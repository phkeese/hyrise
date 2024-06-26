#pragma once

#include <memory>
#include <type_traits>

#include <boost/hana/contains.hpp>
#include <boost/hana/equal.hpp>
#include <boost/hana/fold_left.hpp>
#include <boost/hana/for_each.hpp>

#include "all_type_variant.hpp"
#include "storage/abstract_encoded_segment.hpp"
#include "storage/abstract_segment.hpp"
#include "storage/pos_lists/entire_chunk_pos_list.hpp"
#include "storage/pos_lists/row_id_pos_list.hpp"
#include "storage/reference_segment.hpp"
#include "storage/resolve_encoded_segment_type.hpp"
#include "storage/value_segment.hpp"
#include "utils/assert.hpp"

namespace hyrise {

namespace hana = boost::hana;

/**
 * Resolves a data type by passing a hana::type object on to a generic lambda
 *
 * @param data_type is an enum value of any of the supported column types
 * @param functor is a generic lambda or similar accepting a hana::type object
 *
 *
 * Note on hana::type (taken from Boost.Hana documentation):
 *
 * For subtle reasons having to do with ADL, the actual representation of hana::type is
 * implementation-defined. In particular, hana::type may be a dependent type, so one
 * should not attempt to do pattern matching on it. However, one can assume that hana::type
 * inherits from hana::basic_type, which can be useful when declaring overloaded functions.
 *
 * This means that we need to use hana::basic_type as a parameter in methods so that the
 * underlying type can be deduced from the object.
 *
 *
 * Note on generic lambdas (taken from paragraph 5.1.2/5 of the C++14 Standard Draft n3690):
 *
 * For a generic lambda, the closure type has a public inline function call operator member template (14.5.2)
 * whose template-parameter-list consists of one invented type template-parameter for each occurrence of auto
 * in the lambda’s parameter-declaration-clause, in order of appearance. Example:
 *
 *   auto lambda = [] (auto a) { return a; };
 *
 *   class // unnamed {
 *    public:
 *     template<typename T>
 *     auto operator()(T a) const { return a; }
 *   };
 *
 *
 * Example:
 *
 *   template <typename T>
 *   process_variant(const T& var);
 *
 *   template <typename T>
 *   process_type(hana::basic_type<T> type);  // note: parameter type needs to be hana::basic_type not hana::type!
 *
 *   resolve_data_type(data_type, [&](auto type) {
 *     using ColumnDataType = typename decltype(type)::type;
 *     const auto var = boost::get<ColumnDataType>(variant_from_elsewhere);
 *     process_variant(var);
 *
 *     process_type(type);
 *   });
 */
template <typename Functor>
void resolve_data_type(DataType data_type, const Functor& functor) {
  DebugAssert(data_type != DataType::Null, "data_type cannot be null.");

  hana::for_each(data_type_pairs, [&](auto data_type_pair) {
    if (hana::first(data_type_pair) == data_type) {
      // The + before hana::second - which returns a reference - converts its return value into a value
      functor(+hana::second(data_type_pair));
      return;
    }
  });
}

/**
 * Given a AbstractSegment and its known column type, resolve the segment implementation and call the lambda
 *
 * @param functor is a generic lambda or similar accepting a reference to a specialized segment (value, dictionary,
 * reference)
 *
 *
 * Example:
 *
 *   template <typename T>
 *   void process_segment(ValueSegment<T>& segment);
 *
 *   template <typename T>
 *   void process_segment(DictionarySegment<T>& segment);
 *
 *   void process_segment(ReferenceSegment& segment);
 *
 *   resolve_segment_type<T>(abstract_segment, [&](auto& typed_segment) {
 *     process_segment(typed_segment);
 *   });
 */
template <typename In, typename Out>
using ConstOutIfConstIn = std::conditional_t<std::is_const_v<In>, const Out, Out>;

template <typename ColumnDataType, typename AbstractSegmentType, typename Functor>
// AbstractSegmentType allows segment to be const and non-const
std::enable_if_t<std::is_same_v<AbstractSegment, std::remove_const_t<AbstractSegmentType>>>
/*void*/ resolve_segment_type(AbstractSegmentType& segment, const Functor& functor) {
  using ValueSegmentPtr = ConstOutIfConstIn<AbstractSegmentType, ValueSegment<ColumnDataType>>*;
  using ReferenceSegmentPtr = ConstOutIfConstIn<AbstractSegmentType, ReferenceSegment>*;
  using EncodedSegmentPtr = ConstOutIfConstIn<AbstractSegmentType, AbstractEncodedSegment>*;

  if (const auto value_segment = dynamic_cast<ValueSegmentPtr>(&segment)) {
    functor(*value_segment);
  } else if (const auto reference_segment = dynamic_cast<ReferenceSegmentPtr>(&segment)) {
    functor(*reference_segment);
  } else if (const auto encoded_segment = dynamic_cast<EncodedSegmentPtr>(&segment)) {
    resolve_encoded_segment_type<ColumnDataType>(*encoded_segment, functor);
  } else {
    Fail("Unrecognized column type encountered.");
  }
}

// Used as a template parameter that is passed whenever we conditionally erase the type of the position list. This is
// done to reduce the compile time at the cost of the runtime performance. We do not re-use EraseTypes here, as it
// might confuse readers who could think that the setting erases all types within the functor.
enum class ErasePosListType { OnlyInDebugBuild, Always };

template <ErasePosListType erase_pos_list_type = ErasePosListType::OnlyInDebugBuild, typename Functor>
void resolve_pos_list_type(const std::shared_ptr<const AbstractPosList>& untyped_pos_list, const Functor& functor) {
  if constexpr (HYRISE_DEBUG || erase_pos_list_type == ErasePosListType::Always) {
    functor(untyped_pos_list);
  } else {
    if (const auto row_id_pos_list = std::dynamic_pointer_cast<const RowIDPosList>(untyped_pos_list);
        !untyped_pos_list || row_id_pos_list) {
      // We also use this branch for nullptr instead of calling the functor with the untyped_pos_list. This way, we
      // avoid initializing the functor template with AbstractPosList. The first thing the functor has to do is to
      // check for nullptr anyway, and for that check it does not matter "which" nullptr we pass in.
      functor(row_id_pos_list);
    } else if (const auto entire_chunk_pos_list =
                   std::dynamic_pointer_cast<const EntireChunkPosList>(untyped_pos_list)) {
      functor(entire_chunk_pos_list);
    } else {
      Fail("Unrecognized PosList type encountered");
    }
  }
}

/**
 * Resolves a data type by passing a hana::type object and the downcasted segment on to a generic lambda
 *
 * @param data_type is an enum value of any of the supported column types
 * @param functor is a generic lambda or similar accepting two parameters: a hana::type object and
 *   a reference to a specialized segment (value, dictionary, reference)
 *
 *
 * Example:
 *
 *   template <typename T>
 *   void process_segment(hana::basic_type<T> type, ValueSegment<T>& segment);
 *
 *   template <typename T>
 *   void process_segment(hana::basic_type<T> type, DictionarySegment<T>& segment);
 *
 *   template <typename T>
 *   void process_segment(hana::basic_type<T> type, ReferenceSegment& segment);
 *
 *   resolve_data_and_segment_type(abstract_segment, [&](auto type, auto& typed_segment) {
 *     process_segment(type, typed_segment);
 *   });
 */
template <typename Functor,
          typename AbstractSegmentType>  // AbstractSegmentType allows segment to be const and non-const
std::enable_if_t<std::is_same_v<AbstractSegment, std::remove_const_t<AbstractSegmentType>>>
/*void*/ resolve_data_and_segment_type(AbstractSegmentType& segment, const Functor& functor) {
  resolve_data_type(segment.data_type(), [&](auto type) {
    using ColumnDataType = typename decltype(type)::type;

    resolve_segment_type<ColumnDataType>(segment, [&](auto& typed_segment) {
      functor(type, typed_segment);
    });
  });
}

/**
 * This function returns the DataType of a data type based on the definition in data_type_pairs.
 */
template <typename T>
constexpr DataType data_type_from_type() {
  static_assert(hana::contains(data_types, hana::type_c<T>), "Type not a valid column type.");

  return hana::fold_left(data_type_pairs, DataType{}, [](auto data_type, auto type_tuple) {
    // check whether T is one of the column types
    if (hana::type_c<T> == hana::second(type_tuple)) {
      return hana::first(type_tuple);
    }

    return data_type;
  });
}

}  // namespace hyrise
