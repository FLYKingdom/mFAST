// Copyright (c) 2013, 2014, Huang-Ming Huang,  Object Computing, Inc.
// All rights reserved.
//
// This file is part of mFAST.
//
//     mFAST is free software: you can redistribute it and/or modify
//     it under the terms of the GNU Lesser General Public License as published by
//     the Free Software Foundation, either version 3 of the License, or
//     (at your option) any later version.
//
//     mFAST is distributed in the hope that it will be useful,
//     but WITHOUT ANY WARRANTY; without even the implied warranty of
//     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//     GNU General Public License for more details.
//
//     You should have received a copy of the GNU Lesser General Public License
//     along with mFast.  If not, see <http://www.gnu.org/licenses/>.
//

#include <boost/test/test_tools.hpp>
#include <boost/test/unit_test.hpp>

#include <mfast/int_ref.h>
#include <mfast/coder/common/codec_helper.h>
#include <mfast/coder/encoder/fast_ostream.h>
#include <mfast/coder/encoder/encoder_field_operator.h>
#include <mfast/coder/encoder/encoder_presence_map.h>
#include <cstring>
#include <stdexcept>
#include "debug_allocator.h"
#include "byte_stream.h"
#include "mfast/output.h"
#include "mfast/coder/encoder_v2/fast_encoder_core.h"

using namespace mfast;



enum prev_value_status_enum_t { CHANGE_PREVIOUS_VALUE, PRESERVE_PREVIOUS_VALUE };

template <typename MREF>
boost::test_tools::predicate_result
encode_mref(const byte_stream&       result_stream,
            const MREF&              value,
            prev_value_status_enum_t prev_status)
{
  char buffer[32];

  mfast::allocator* alloc = value.allocator();
  fast_ostreambuf sb(buffer);
  fast_ostream strm(alloc);
  strm.rdbuf(&sb);

  encoder_presence_map pmap;
  pmap.init(&strm, 7);

  typename MREF::instruction_cptr instruction = value.instruction();
  value_storage& prev_storage = const_cast<value_storage&>(instruction->prev_value());
  value_storage old_prev_storage;
  if (prev_storage.is_defined())
    instruction->copy_construct_value(prev_storage, old_prev_storage, alloc);

  typename MREF::cref_type old_prev( &old_prev_storage, instruction);


  encoder_operators[instruction->field_operator()]->encode(value, strm, pmap);
  pmap.commit();

  boost::test_tools::predicate_result res( false );

  typename MREF::cref_type prev( &instruction->prev_value(), instruction);

  if (result_stream  != byte_stream(sb) ) {
    res.message()<< "encoded byte string \"" << byte_stream(sb) << "\" does not match expected";
  }
  else if (prev_status == PRESERVE_PREVIOUS_VALUE) {
    if (!old_prev_storage.is_defined()) {
      if (instruction->prev_value().is_defined()) {
        res.message() << "previous value changed after encoding";
      }
    }
    else if (!instruction->prev_value().is_defined()) {
      res.message() << "previous value not defined after encoding";
    }
    else if (old_prev != prev) {
      res.message() << "previous value changed after encoding";
    }
  }
  else if (prev != value) {
    res.message() << "previous value is not properly set after encoding";
  }

  res = res.has_empty_message();

  instruction->destruct_value(prev_storage,alloc);
  instruction->destruct_value(old_prev_storage,alloc);
  prev_storage.of_array.capacity_in_bytes_ = 0;

  return res;
}

template <typename EXT_REF>
boost::test_tools::predicate_result
encode_ext_cref(const byte_stream&       result_stream,
                const EXT_REF&           ext_ref,
                prev_value_status_enum_t prev_status,
                mfast::allocator* alloc)
{
  char buffer[32];

  typedef typename EXT_REF::cref_type CREF;
  CREF value = ext_ref.get();

  mfast::coder::fast_encoder_core core(alloc);

  fast_ostreambuf sb(buffer);
  core.strm_.rdbuf(&sb);

  encoder_presence_map pmap;
  pmap.init(&core.strm_, 7);

  typename CREF::instruction_cptr instruction = value.instruction();
  value_storage& prev_storage = const_cast<value_storage&>(instruction->prev_value());
  value_storage old_prev_storage;
  if (prev_storage.is_defined())
    instruction->copy_construct_value(prev_storage, old_prev_storage, alloc);

  CREF old_prev( &old_prev_storage, instruction);
  core.current_ = &pmap;
  core.visit(ext_ref);

  pmap.commit();

  boost::test_tools::predicate_result res( false );

  CREF prev( &instruction->prev_value(), instruction);

  if (result_stream  != byte_stream(sb) ) {
    res.message()<< "encoded byte string \"" << byte_stream(sb) << "\" does not match expected";
  }
  else if (prev_status == PRESERVE_PREVIOUS_VALUE) {
    if (!old_prev_storage.is_defined()) {
      if (instruction->prev_value().is_defined()) {
        res.message() << "previous value changed after encoding";
      }
    }
    else if (!instruction->prev_value().is_defined()) {
      res.message() << "previous value not defined after encoding";
    }
    else if (old_prev != prev) {
      res.message() << "previous value changed after encoding";
    }
  }
  else if (prev != value) {
    res.message() << "previous value is not properly set after encoding";
  }

  res = res.has_empty_message();

  instruction->destruct_value(prev_storage,alloc);
  instruction->destruct_value(old_prev_storage,alloc);
  prev_storage.of_array.capacity_in_bytes_ = 0;

  return res;
}

typedef properties_type<3> optional_with_initial_value_tag;
typedef properties_type<1> optional_without_initial_value_tag;
typedef properties_type<0> mandatory_without_initial_value_tag;
typedef properties_type<2> mandatory_with_initial_value_tag;


BOOST_AUTO_TEST_SUITE( test_encoder_encode_operator )

BOOST_AUTO_TEST_CASE(operator_none_test)
{
  malloc_allocator allocator;
  value_storage storage;

  {
    uint64_field_instruction inst(operator_none,
                                  presence_optional,
                                  1,
                                  "test_uint64","",
                                  0,
                                  int_value_storage<uint64_t>(UINT64_MAX));
    inst.construct_value(storage, &allocator);

    // If a field is optional and has no field operator, it is encoded with a
    // nullable representation and the NULL is used to represent absence of a
    // value. It will not occupy any bits in the presence map.

    uint64_mref result(&allocator, &storage, &inst);
    result.omit();

    BOOST_CHECK( encode_mref("\x80\x80", result, CHANGE_PREVIOUS_VALUE) );

    inst.prev_value().defined(false); // reset the previous value to undefined again

    BOOST_CHECK( encode_ext_cref("\x80\x80",
                                 ext_cref<uint64_cref, none_operator_tag, optional_with_initial_value_tag >(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );

  }
  {
    uint64_field_instruction inst(operator_none,
                                  presence_mandatory,
                                  1,
                                  "test_uint64","",
                                  0,
                                  int_value_storage<uint64_t>(UINT64_MAX));
    inst.construct_value(storage, &allocator);

    // If a field is optional and has no field operator, it is encoded with a
    // nullable representation and the NULL is used to represent absence of a
    // value. It will not occupy any bits in the presence map.

    uint64_mref result(&allocator, &storage, &inst);
    result.as(0);
    BOOST_CHECK( encode_mref("\x80\x80", result, CHANGE_PREVIOUS_VALUE) );
    inst.prev_value().defined(false); // reset the previous value to undefined again
    BOOST_CHECK( encode_ext_cref("\x80\x80",
                                 ext_cref<uint64_cref, none_operator_tag, mandatory_with_initial_value_tag >(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );

  }
}

BOOST_AUTO_TEST_CASE(operator_constant_encode_test)
{

  malloc_allocator allocator;
  value_storage storage;

  {
    // An optional field with the constant operator will occupy a single bit. If the bit is set, the value
    // is the initial value in the instruction context. If the bit is not set, the value is considered absent.

    uint64_field_instruction inst(operator_constant,
                                  presence_optional,
                                  1,
                                  "test_uint64","",
                                  0,
                                  int_value_storage<uint64_t>(UINT64_MAX));
    inst.construct_value(storage, &allocator);


    uint64_mref result(&allocator, &storage, &inst);
    result.as(UINT64_MAX);

    // testing when the presence bit is set
    BOOST_CHECK( encode_mref("\xC0",result, CHANGE_PREVIOUS_VALUE) );

    inst.prev_value().defined(false); // reset the previous value to undefined again
    BOOST_CHECK( encode_ext_cref("\xC0",
                                 ext_cref<uint64_cref, constant_operator_tag, optional_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );

    // testing when the presence bit is not set

    result.omit();
    BOOST_CHECK( encode_mref("\x80", result, CHANGE_PREVIOUS_VALUE) );

    inst.prev_value().defined(false); // reset the previous value to undefined again
    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<uint64_cref, constant_operator_tag, optional_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
  }
  {
    // A field will not occupy any bit in the presence map if it is mandatory and has the constant operator.

    uint64_field_instruction inst(operator_constant,
                                  presence_mandatory,
                                  1,
                                  "test_uint64","",
                                  0,
                                  int_value_storage<uint64_t>(UINT64_MAX));
    inst.construct_value(storage, &allocator);


    uint64_mref result(&allocator, &storage, &inst);
    BOOST_CHECK( !result.optional());

    result.as(UINT64_MAX);
    BOOST_CHECK( encode_mref("\x80", result, CHANGE_PREVIOUS_VALUE) );
    inst.prev_value().defined(false); // reset the previous value to undefined again

    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<uint64_cref, constant_operator_tag, mandatory_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
  }
}

BOOST_AUTO_TEST_CASE(operator_default_encode_test)
{

  malloc_allocator allocator;
  value_storage storage;

  {
    uint64_field_instruction inst(operator_default,
                                  presence_mandatory,
                                  1,
                                  "test_uint64","",
                                  0,
                                  int_value_storage<uint64_t>(UINT64_MAX));
    inst.construct_value(storage, &allocator);


    uint64_mref result(&allocator, &storage, &inst);

    // Mandatory integer, decimal, string and byte vector fields – one bit. If set, the value appears in the stream.
    result.as(0);
    BOOST_CHECK( encode_mref("\xC0\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false); // reset the previous value to undefined again

    BOOST_CHECK( encode_ext_cref("\xC0\x80",
                                 ext_cref<uint64_cref, default_operator_tag, mandatory_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
  }

  {
    uint64_field_instruction inst(operator_default,
                                  presence_mandatory,
                                  1,
                                  "test_uint64","",
                                  0,
                                  int_value_storage<uint64_t>(UINT64_MAX));
    inst.construct_value(storage, &allocator);


    uint64_mref result(&allocator, &storage, &inst);
    // Mandatory integer, decimal, string and byte vector fields – one bit. If set, the value appears in the stream.

    result.as(UINT64_MAX);
    BOOST_CHECK(encode_mref("\x80",  result,  CHANGE_PREVIOUS_VALUE ) );

    inst.prev_value().defined(false); // reset the previous value to undefined again

    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<uint64_cref, default_operator_tag, mandatory_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
  }

  {
    uint64_field_instruction inst(operator_default,
                                  presence_optional,
                                  1,
                                  "test_uint64","",
                                  0,
                                  int_value_storage<uint64_t>(UINT64_MAX));
    inst.construct_value(storage, &allocator);


    uint64_mref result(&allocator, &storage, &inst);

    result.omit();
    // Optional integer, decimal, string and byte vector fields – one bit.
    // If set, the value appears in the stream in a nullable representation.
    // A NULL indicates that the value is absent and the state of the previous
    // value  is left unchanged.
    BOOST_CHECK(encode_mref("\xC0\x80",  result,  PRESERVE_PREVIOUS_VALUE ) );

    inst.prev_value().defined(false); // reset the previous value to undefined again

    BOOST_CHECK( encode_ext_cref("\xC0\x80",
                                 ext_cref<uint64_cref, default_operator_tag, optional_with_initial_value_tag>(result),
                                 PRESERVE_PREVIOUS_VALUE, &allocator ) );
  }

  {
    uint64_field_instruction inst(operator_default,
                                  presence_optional,
                                  1,
                                  "test_uint64","",
                                  0,
                                  int_value_storage<uint64_t>(UINT64_MAX));
    inst.construct_value(storage, &allocator);


    uint64_mref result(&allocator, &storage, &inst);

    // Optional integer, decimal, string and byte vector fields – one bit.

    // The default operator specifies that the value of a field is either present in the stream or it will be the initial value.

    result.as(UINT64_MAX);
    BOOST_CHECK(encode_mref("\x80",  result,  CHANGE_PREVIOUS_VALUE ) );

    inst.prev_value().defined(false); // reset the previous value to undefined again

    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<uint64_cref, default_operator_tag, optional_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
  }

  {
    uint64_field_instruction inst(operator_default,
                                  presence_optional,
                                  1,
                                  "test_uint64","",
                                  0, int_value_storage<uint64_t>());
    inst.construct_value(storage, &allocator);


    uint64_mref result(&allocator, &storage, &inst);

    // If the field has optional presence and no initial value, the field is considered absent when there is no value in the stream.

    result.omit();
    BOOST_CHECK( encode_mref("\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false); // reset the previous value to undefined again

    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<uint64_cref, default_operator_tag, optional_without_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
  }
}

BOOST_AUTO_TEST_CASE(operator_copy_encode_test)
{
  malloc_allocator allocator;
  value_storage storage;

  {
    uint64_field_instruction inst(operator_copy,
                                  presence_mandatory,
                                  1,
                                  "test_uint64","",
                                  0,
                                  int_value_storage<uint64_t>(UINT64_MAX));
    inst.construct_value(storage, &allocator);


    uint64_mref result(&allocator, &storage, &inst);
    result.as(0);

    // Mandatory integer, decimal, string and byte vector fields – one bit. If set, the value appears in the stream.
    BOOST_CHECK(encode_mref("\xC0\x80",  result,  CHANGE_PREVIOUS_VALUE ) );

    inst.prev_value().defined(false); // reset the previous value to undefined again

    BOOST_CHECK( encode_ext_cref("\xC0\x80",
                                 ext_cref<uint64_cref, copy_operator_tag, mandatory_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
  }

  {
    uint64_field_instruction inst(operator_copy,
                                  presence_mandatory,
                                  1,
                                  "test_uint64","",
                                  0,
                                  int_value_storage<uint64_t>(UINT64_MAX));
    inst.construct_value(storage, &allocator);

    uint64_mref result(&allocator, &storage, &inst);
    result.as(UINT64_MAX);

    // Mandatory integer, decimal, string and byte vector fields – one bit. If set, the value appears in the stream.

    // When the value is not present in the stream there are three cases depending on the state of the previous value:
    // * undefined – the value of the field is the initial value that also becomes the new previous value.
    BOOST_CHECK(encode_mref("\x80",  result,  CHANGE_PREVIOUS_VALUE ) );

    inst.prev_value().defined(false); // reset the previous value to undefined again

    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<uint64_cref, copy_operator_tag, mandatory_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );

    uint64_mref prev(&allocator, &inst.prev_value(), &inst);
    /// set previous value to a different value
    prev.as(5);
    result.as(5);

    // When the value is not present in the stream there are three cases depending on the state of the previous value:
    // * assigned – the value of the field is the previous value.
    BOOST_CHECK( encode_mref("\x80",  result,  CHANGE_PREVIOUS_VALUE ) );

    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<uint64_cref, copy_operator_tag, mandatory_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );

    /// set previous value to empty
    inst.prev_value().present(false);    // // When the value is not present in the stream there are three cases depending on the state of the previous value:
    // // * empty – the value of the field is empty. If the field is optional the value is considered absent. It is a dynamic error [ERR D6] if the field is mandatory.

    BOOST_CHECK_THROW( encode_mref("\x80", result, CHANGE_PREVIOUS_VALUE), mfast::fast_error );
    inst.prev_value().present(false);    // // When the value is not present in the stream there are three cases depending on the state of the previous value:
    BOOST_CHECK_THROW( encode_ext_cref("\x80",
                                       ext_cref<uint64_cref, copy_operator_tag, mandatory_with_initial_value_tag>(result),
                                       CHANGE_PREVIOUS_VALUE, &allocator )
                       , mfast::fast_error );
  }

  {
    uint64_field_instruction inst(operator_copy,
                                  presence_optional,
                                  1,
                                  "test_uint64","",
                                  0,
                                  int_value_storage<uint64_t>(UINT64_MAX));
    inst.construct_value(storage, &allocator);


    uint64_mref result(&allocator, &storage, &inst);
    result.omit();
    // Optional integer, decimal, string and byte vector fields – one bit.
    // If set, the value appears in the stream in a nullable representation.
    // A NULL indicates that the value is absent and the state of the previous value is set to empty
    BOOST_CHECK( encode_mref("\xC0\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\xC0\x80",
                                 ext_cref<uint64_cref, copy_operator_tag, optional_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
  }

  {
    uint64_field_instruction inst(operator_copy,
                                  presence_optional,
                                  1,
                                  "test_uint64","",
                                  0,
                                  int_value_storage<uint64_t>(UINT64_MAX));
    inst.construct_value(storage, &allocator);


    uint64_mref result(&allocator, &storage, &inst);
    result.as(UINT64_MAX);
    // Optional integer, decimal, string and byte vector fields – one bit.
    // If set, the value appears in the stream in a nullable representation.
    // A NULL indicates that the value is absent and the state of the previous value is set to empty

    // When the value is not present in the stream there are three cases depending on the state of the previous value:
    // * undefined – the value of the field is the initial value that also becomes the new previous value.
    BOOST_CHECK( encode_mref("\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);

    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<uint64_cref, copy_operator_tag, optional_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
    uint64_mref prev(&allocator, &inst.prev_value(), &inst);
    /// set previous value to a different value
    prev.as(5);

    // When the value is not present in the stream there are three cases depending on the state of the previous value:
    // * assigned – the value of the field is the previous value.
    result.as(5);
    BOOST_CHECK( encode_mref("\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<uint64_cref, copy_operator_tag, optional_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
    /// set previous value to empty
    inst.prev_value().present(false);
    // When the value is not present in the stream there are three cases depending on the state of the previous value:
    // * empty – the value of the field is empty. If the field is optional the value is considered absent.
    result.omit();
  }

  { // testing no initial value
    uint64_field_instruction inst(operator_copy,
                                  presence_optional,
                                  1,
                                  "test_uint64","",
                                  0, int_value_storage<uint64_t>());
    inst.construct_value(storage, &allocator);


    uint64_mref result(&allocator, &storage, &inst);
    result.omit();

    // When the value is not present in the stream there are three cases depending on the state of the previous value:
    // * undefined – If the field has optional presence and no initial value, the field is considered absent and the state of the previous value is changed to empty.
    BOOST_CHECK( encode_mref("\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<uint64_cref, copy_operator_tag, optional_without_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
  }
}

BOOST_AUTO_TEST_CASE(operator_increment_encode_test)
{
  malloc_allocator allocator;
  value_storage storage;

  {
    uint64_field_instruction inst(operator_increment,
                                  presence_mandatory,
                                  1,
                                  "test_uint64","",
                                  0,
                                  int_value_storage<uint64_t>(UINT64_MAX));
    inst.construct_value(storage, &allocator);


    uint64_mref result(&allocator, &storage, &inst);
    result.as(0);
    // Mandatory integer, decimal, string and byte vector fields – one bit. If set, the value appears in the stream.

    BOOST_CHECK( encode_mref("\xC0\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\xC0\x80",
                                 ext_cref<uint64_cref, increment_operator_tag, mandatory_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
  }

  {
    uint64_field_instruction inst(operator_increment,
                                  presence_mandatory,
                                  1,
                                  "test_uint64","",
                                  0,
                                  int_value_storage<uint64_t>(UINT64_MAX));
    inst.construct_value(storage, &allocator);


    uint64_mref result(&allocator, &storage, &inst);
    result.as(UINT64_MAX);
    // Mandatory integer, decimal, string and byte vector fields – one bit. If set, the value appears in the stream.

    // When the value is not present in the stream there are three cases depending on the state of the previous value:
    // * undefined – the value of the field is the initial value that also becomes the new previous value.
    BOOST_CHECK( encode_mref("\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<uint64_cref, increment_operator_tag, mandatory_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
    uint64_mref prev(&allocator, &inst.prev_value(), &inst);
    /// set previous value to a different value
    prev.as(5);

    result.as(6);
    // Mandatory integer, decimal, string and byte vector fields – one bit. If set, the value appears in the stream.

    // When the value is not present in the stream there are three cases depending on the state of the previous value:
    // * assigned – the value of the field is the previous value incremented by one. The incremented value also becomes the new previous value.
    BOOST_CHECK( encode_mref("\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    prev.as(5);
    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<uint64_cref, increment_operator_tag, mandatory_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
    /// set previous value to empty
    inst.prev_value().present(false);
    // When the value is not present in the stream there are three cases depending on the state of the previous value:
    // * empty – the value of the field is empty. If the field is optional the value is considered absent. It is a dynamic error [ERR D6] if the field is mandatory.
    BOOST_CHECK_THROW( encode_mref("\x80\x80", result, CHANGE_PREVIOUS_VALUE), mfast::fast_error);
    inst.prev_value().present(false);
    BOOST_CHECK_THROW( encode_ext_cref("\x80\x80",
                                       ext_cref<uint64_cref, increment_operator_tag, mandatory_with_initial_value_tag>(result),
                                       CHANGE_PREVIOUS_VALUE, &allocator ),
                       mfast::fast_error);
  }

  {
    uint64_field_instruction inst(operator_increment,
                                  presence_optional,
                                  1,
                                  "test_uint64","",
                                  0,
                                  int_value_storage<uint64_t>(UINT64_MAX));
    inst.construct_value(storage, &allocator);


    uint64_mref result(&allocator, &storage, &inst);
    result.omit();
    // Optional integer, decimal, string and byte vector fields – one bit.
    // If set, the value appears in the stream in a nullable representation.
    // A NULL indicates that the value is absent and the state of the previous value is set to empty

    BOOST_CHECK( encode_mref("\xC0\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\xC0\x80",
                                 ext_cref<uint64_cref, increment_operator_tag, optional_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
  }

  {
    uint64_field_instruction inst(operator_increment,
                                  presence_optional,
                                  1,
                                  "test_uint64","",
                                  0,
                                  int_value_storage<uint64_t>(UINT64_MAX));
    inst.construct_value(storage, &allocator);


    uint64_mref result(&allocator, &storage, &inst);
    result.as(UINT64_MAX);
    // Optional integer, decimal, string and byte vector fields – one bit.
    // A NULL indicates that the value is absent and the state of the previous value is set to empty

    // When the value is not present in the stream there are three cases depending on the state of the previous value:
    // * undefined – the value of the field is the initial value that also becomes the new previous value.
    BOOST_CHECK( encode_mref("\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<uint64_cref, increment_operator_tag, optional_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
    uint64_mref prev(&allocator, &inst.prev_value(), &inst);

    /// set previous value to a different value
    prev.as(5);
    // When the value is not present in the stream there are three cases depending on the state of the previous value:
    // * assigned – the value of the field is the previous value incremented by one. The incremented value also becomes the new previous value.

    result.as(6);
    BOOST_CHECK( encode_mref("\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    prev.as(5);
    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<uint64_cref, increment_operator_tag, optional_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
    /// set previous value to empty
    prev.omit();
    // When the value is not present in the stream there are three cases depending on the state of the previous value:
    // * empty – the value of the field is empty. If the field is optional the value is considered absent.
    result.omit();
    BOOST_CHECK( encode_mref("\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    /// set previous value to empty
    prev.omit();
    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<uint64_cref, increment_operator_tag, optional_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );

  }

  { // testing no initial value
    uint64_field_instruction inst(operator_increment,
                                  presence_optional,
                                  1,
                                  "test_uint64","",
                                  0, int_value_storage<uint64_t>());
    inst.construct_value(storage, &allocator);


    uint64_mref result(&allocator, &storage, &inst);
    result.omit();
    // Optional integer, decimal, string and byte vector fields – one bit.
    // A NULL indicates that the value is absent and the state of the previous value is set to empty

    // When the value is not present in the stream there are three cases depending on the state of the previous value:
    // * undefined – If the field has optional presence and no initial value, the field is considered absent and the state of the previous value is changed to empty.

    BOOST_CHECK( encode_mref("\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<uint64_cref, increment_operator_tag, optional_without_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
  }
}

BOOST_AUTO_TEST_CASE(operator_delta_integer_encode_test)
{
  malloc_allocator allocator;
  value_storage storage;

  {
    uint64_field_instruction inst(operator_delta,
                                  presence_mandatory,
                                  1,
                                  "test_uint64","",
                                  0,
                                  int_value_storage<uint64_t>(5ULL));
    inst.construct_value(storage, &allocator);


    uint64_mref result(&allocator, &storage, &inst);
    // Mandatory integer, decimal, string and byte vector fields – no bit.


    // the field is obtained by combining the delta value with a base value.
    // The base value depends on the state of the previous value in the following way:
    //  undefined – the base value is the initial value if present in the instruction context. Otherwise a type dependant default base value is used.
    result.as(7);
    BOOST_CHECK( encode_mref("\x80\x82",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80\x82",
                                 ext_cref<uint64_cref, delta_operator_tag, mandatory_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
  }

  {
    uint64_field_instruction inst(operator_delta,
                                  presence_mandatory,
                                  1,
                                  "test_uint64","",
                                  0, int_value_storage<uint64_t>()); // no initial value
    inst.construct_value(storage, &allocator);


    uint64_mref result(&allocator, &storage, &inst);
    // Mandatory integer, decimal, string and byte vector fields – no bit.


    // the field is obtained by combining the delta value with a base value.
    // The base value depends on the state of the previous value in the following way:
    //  undefined – the base value is the initial value if present in the instruction context. Otherwise a type dependant default base value is used.

    result.as(2);
    BOOST_CHECK( encode_mref("\x80\x82",  result,  CHANGE_PREVIOUS_VALUE ) );

    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80\x82",
                                 ext_cref<uint64_cref, delta_operator_tag, mandatory_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
  }

  {
    uint64_field_instruction inst(operator_delta,
                                  presence_optional,
                                  1,
                                  "test_uint64","",
                                  0,
                                  int_value_storage<uint64_t>(5ULL));
    inst.construct_value(storage, &allocator);


    uint64_mref result(&allocator, &storage, &inst);
    //Optional integer, decimal, string and byte vector fields – no bit.
    // The delta appears in the stream in a nullable representation. A NULL indicates that the delta is absent.


    // the field is obtained by combining the delta value with a base value.
    // The base value depends on the state of the previous value in the following way:
    //  undefined – the base value is the initial value if present in the instruction context. Otherwise a type dependant default base value is used.

    result.as(6);
    BOOST_CHECK( encode_mref("\x80\x82",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80\x82",
                                 ext_cref<uint64_cref, delta_operator_tag, optional_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
  }

  {
    uint64_field_instruction inst(operator_delta,
                                  presence_optional,
                                  1,
                                  "test_uint64","",
                                  0,
                                  int_value_storage<uint64_t>(5ULL));
    inst.construct_value(storage, &allocator);


    uint64_mref result(&allocator, &storage, &inst);
    //Optional integer, decimal, string and byte vector fields – no bit.
    // The delta appears in the stream in a nullable representation. A NULL indicates that the delta is absent.


    //  If the field has optional presence, the delta value can be NULL. In that case the value of the field is considered absent.

    result.omit();
    BOOST_CHECK( encode_mref("\x80\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80\x80",
                                 ext_cref<uint64_cref, delta_operator_tag, optional_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
  }

}

BOOST_AUTO_TEST_CASE(operator_delta_decimal_encode_test)
{
  malloc_allocator allocator;
  value_storage storage;

  {
    decimal_field_instruction inst(operator_delta,
                                   presence_mandatory,
                                   1,
                                   "test_decimal","",
                                   0,
                                   decimal_value_storage(12,1)); //  initial

    inst.construct_value(storage, &allocator);

    decimal_mref result(&allocator, &storage, &inst);
    // Mandatory integer, decimal, string and byte vector fields – no bit.

    // the field is obtained by combining the delta value with a base value.
    // The base value depends on the state of the previous value in the following way:
    //  undefined – the base value is the initial value if present in the instruction context. Otherwise a type dependant default base value is used.
    result.as(15,3); // 15 = 12 (base) + 3 (delta), 3= 1 (base) + 2 (delta)
    BOOST_CHECK( encode_mref("\x80\x82\x83",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80\x82\x83",
                                 ext_cref<decimal_cref, delta_operator_tag, mandatory_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
  }
  {
    decimal_field_instruction inst(operator_delta,
                                   presence_mandatory,
                                   1,
                                   "test_decimal","",
                                   0,
                                   decimal_value_storage()); // no initial value

    inst.construct_value(storage, &allocator);
    decimal_mref result(&allocator, &storage, &inst);
    // Mandatory integer, decimal, string and byte vector fields – no bit.

    // the field is obtained by combining the delta value with a base value.
    // The base value depends on the state of the previous value in the following way:
    //  undefined – the base value is the initial value if present in the instruction context. Otherwise a type dependant default base value is used.
    result.as(3,2); // 3 =  0 (base) + 3 (delta), 2 = 0 (base) + 2 (delta)
    BOOST_CHECK( encode_mref("\x80\x82\x83",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80\x82\x83",
                                 ext_cref<decimal_cref, delta_operator_tag, mandatory_without_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &allocator ) );
  }

  {
    decimal_field_instruction inst(operator_delta,
                                   presence_optional,
                                   1,
                                   "test_decimal","",
                                   0,
                                   decimal_value_storage()); // no initial value

    inst.construct_value(storage, &allocator);
    decimal_mref result(&allocator, &storage, &inst);
    // Optional integer, decimal, string and byte vector fields – no bit.

    // the field is obtained by combining the delta value with a base value.
    // The base value depends on the state of the previous value in the following way:
    //  undefined – the base value is the initial value if present in the instruction context. Otherwise a type dependant default base value is used.
    result.omit();
    BOOST_CHECK( encode_mref("\x80\x80",  result,  PRESERVE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80\x80",
                                 ext_cref<decimal_cref, delta_operator_tag, optional_without_initial_value_tag>(result),
                                 PRESERVE_PREVIOUS_VALUE, &allocator ) );
  }
}

BOOST_AUTO_TEST_CASE(operator_delta_ascii_encode_test)
{
  debug_allocator alloc;
  value_storage storage;

  { // testing mandatory field with initial value
    const char* default_value = "initial_string";
    ascii_field_instruction inst(operator_delta,
                                 presence_mandatory,
                                 1,
                                 "test_ascii","",
                                 0,
                                 string_value_storage(default_value));

    inst.construct_value(storage, &alloc);

    ascii_string_mref result(&alloc, &storage, &inst);
    // Mandatory integer, decimal, string and byte vector fields – no bit.

    // the field is obtained by combining the delta value with a base value.
    // The base value depends on the state of the previous value in the following way:
    //  undefined – the base value is the initial value if present in the instruction context. Otherwise a type dependant default base value is used.
    result.to_initial_value();
    BOOST_CHECK( encode_mref("\x80\x80\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80\x80\x80",
                                 ext_cref<ascii_string_cref, delta_operator_tag, mandatory_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &alloc ) );
    inst.destruct_value(storage, &alloc);
  }

  { // testing mandatory field with initial value
    const char* default_value = "initial_string";
    ascii_field_instruction inst(operator_delta,
                                 presence_mandatory,
                                 1,
                                 "test_ascii","",
                                 0,
                                 string_value_storage(default_value));

    inst.construct_value(storage, &alloc);

    ascii_string_mref result(&alloc, &storage, &inst);
    // Mandatory integer, decimal, string and byte vector fields – no bit.

    // the field is obtained by combining the delta value with a base value.
    // The base value depends on the state of the previous value in the following way:
    //  undefined – the base value is the initial value if present in the instruction context. Otherwise a type dependant default base value is used.
    result.as("initial_value");
    BOOST_CHECK( encode_mref("\x80\x86\x76\x61\x6C\x75\xE5", result, CHANGE_PREVIOUS_VALUE) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80\x86\x76\x61\x6C\x75\xE5",
                                 ext_cref<ascii_string_cref, delta_operator_tag, mandatory_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &alloc ) );
    inst.destruct_value(storage, &alloc);
  }

  { // testing mandatory field without initial value

    ascii_field_instruction inst(operator_delta,
                                 presence_mandatory,
                                 1,
                                 "test_ascii","",
                                 0, string_value_storage());

    inst.construct_value(storage, &alloc);

    ascii_string_mref result(&alloc, &storage, &inst);
    // Mandatory integer, decimal, string and byte vector fields – no bit.

    // the field is obtained by combining the delta value with a base value.
    // The base value depends on the state of the previous value in the following way:
    //  undefined – the base value is the initial value if present in the instruction context. Otherwise a type dependant default base value is used.
    result.as("ABCD");
    BOOST_CHECK( encode_mref("\x80\x80\x41\x42\x43\xC4", result, CHANGE_PREVIOUS_VALUE) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80\x80\x41\x42\x43\xC4",
                                 ext_cref<ascii_string_cref, delta_operator_tag, mandatory_without_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &alloc ) );
    inst.destruct_value(storage, &alloc);
  }

  { // testing optional field with NULL substraction in the stream
    const char* default_value = "initial_string";
    ascii_field_instruction inst(operator_delta,
                                 presence_optional,
                                 1,
                                 "test_ascii","",
                                 0,
                                 string_value_storage(default_value));

    inst.construct_value(storage, &alloc);

    ascii_string_mref result(&alloc, &storage, &inst);
    // Optional integer, decimal, string and byte vector fields – no bit.

    // If the field has optional presence, the delta value can be NULL.
    // In that case the value of the field is considered absent.
    // Note that the previous value is not set to empty but is left untouched if the value is absent.
    result.omit();
    BOOST_CHECK( encode_mref("\x80\x80",  result,  PRESERVE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80\x80",
                                 ext_cref<ascii_string_cref, delta_operator_tag, optional_with_initial_value_tag>(result),
                                 PRESERVE_PREVIOUS_VALUE, &alloc ) );
    inst.destruct_value(storage, &alloc);
  }
  { // testing optional field with positive substraction in the stream
    const char* default_value = "initial_string";
    ascii_field_instruction inst(operator_delta,
                                 presence_optional,
                                 1,
                                 "test_ascii","",
                                 0,
                                 string_value_storage(default_value));

    inst.construct_value(storage, &alloc);

    ascii_string_mref result(&alloc, &storage, &inst);
    // Optional integer, decimal, string and byte vector fields – no bit.

    // the field is obtained by combining the delta value with a base value.
    // The base value depends on the state of the previous value in the following way:
    //  undefined – the base value is the initial value if present in the instruction context. Otherwise a type dependant default base value is used.

    result.as("initial_striABCD");
    BOOST_CHECK( encode_mref("\x80\x83\x41\x42\x43\xC4",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80\x83\x41\x42\x43\xC4",
                                 ext_cref<ascii_string_cref, delta_operator_tag, optional_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &alloc ) );
    inst.destruct_value(storage, &alloc);
  }

  { // testing optional field with negative substraction in the stream
    const char* default_value = "initial_string";
    ascii_field_instruction inst(operator_delta,
                                 presence_optional,
                                 1,
                                 "test_ascii","",
                                 0,
                                 string_value_storage(default_value));

    inst.construct_value(storage, &alloc);

    ascii_string_mref result(&alloc, &storage, &inst);
    // Optional integer, decimal, string and byte vector fields – no bit.

    // the field is obtained by combining the delta value with a base value.
    // The base value depends on the state of the previous value in the following way:
    //  undefined – the base value is the initial value if present in the instruction context. Otherwise a type dependant default base value is used.

    result.as("ABCD_string");
    BOOST_CHECK( encode_mref("\x80\xF8\x41\x42\x43\xC4",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80\xF8\x41\x42\x43\xC4",
                                 ext_cref<ascii_string_cref, delta_operator_tag, optional_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &alloc ) );
    inst.destruct_value(storage, &alloc);
  }
}

BOOST_AUTO_TEST_CASE(operator_delta_unicode_encode_test)
{
  debug_allocator alloc;
  value_storage storage;

  { // testing mandatory field with initial value
    const char* default_value = "initial_string";
    unicode_field_instruction inst(operator_delta,
                                   presence_mandatory,
                                   1,
                                   "test_ascii","",
                                   0,
                                   string_value_storage(default_value), 0, "", "");

    inst.construct_value(storage, &alloc);

    unicode_string_mref result(&alloc, &storage, &inst);
    // Mandatory integer, decimal, string and byte vector fields – no bit.

    // the field is obtained by combining the delta value with a base value.
    // The base value depends on the state of the previous value in the following way:
    //  undefined – the base value is the initial value if present in the instruction context. Otherwise a type dependant default base value is used.

    result.to_initial_value();
    BOOST_CHECK( encode_mref("\x80\x80\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80\x80\x80",
                                 ext_cref<unicode_string_cref, delta_operator_tag, mandatory_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &alloc ) );
    inst.destruct_value(storage, &alloc);
  }



  { // testing mandatory field without initial value
    //const char* default_value = "initial_string";

    unicode_field_instruction inst(operator_delta,
                                   presence_mandatory,
                                   1,
                                   "test_unicode","",
                                   0,
                                   string_value_storage(), 0, "", "");

    inst.construct_value(storage, &alloc);

    unicode_string_mref result(&alloc, &storage, &inst);
    // Mandatory integer, decimal, string and byte vector fields – no bit.

    // the field is obtained by combining the delta value with a base value.
    // The base value depends on the state of the previous value in the following way:
    //  undefined – the base value is the initial value if present in the instruction context. Otherwise a type dependant default base value is used.

    result.as("ABCD");
    BOOST_CHECK( encode_mref("\x80\x80\x84\x41\x42\x43\x44", result, CHANGE_PREVIOUS_VALUE) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80\x80\x84\x41\x42\x43\x44",
                                 ext_cref<unicode_string_cref, delta_operator_tag, mandatory_without_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &alloc ) );

    inst.destruct_value(storage, &alloc);
  }

  { // testing optional field with NULL substraction in the stream
    const char* default_value = "initial_string";
    unicode_field_instruction inst(operator_delta,
                                   presence_optional,
                                   1,
                                   "test_unicode","",
                                   0,
                                   string_value_storage(default_value), 0, "", "");

    inst.construct_value(storage, &alloc);

    unicode_string_mref result(&alloc, &storage, &inst);
    // Mandatory integer, decimal, string and byte vector fields – no bit.

    // Optional integer, decimal, string and byte vector fields – no bit.
    // The delta appears in the stream in a nullable representation.
    // A NULL indicates that the delta is absent.
    // Note that the previous value is not set to empty but is left untouched if the value is absent.

    // A NULL delta is represented as a NULL subtraction length. The string part is present in the stream iff the subtraction length is not NULL.
    result.omit();
    BOOST_CHECK( encode_mref("\x80\x80",  result,  PRESERVE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80\x80",
                                 ext_cref<unicode_string_cref, delta_operator_tag, optional_with_initial_value_tag>(result),
                                 PRESERVE_PREVIOUS_VALUE, &alloc ) );

    inst.destruct_value(storage, &alloc);
  }
  { // testing optional field with positive substraction in the stream
    const char* default_value = "initial_string";
    unicode_field_instruction inst(operator_delta,
                                   presence_optional,
                                   1,
                                   "test_unicode","",
                                   0,
                                   string_value_storage(default_value), 0, "", "");

    inst.construct_value(storage, &alloc);

    unicode_string_mref result(&alloc, &storage, &inst);
    // Optional integer, decimal, string and byte vector fields – no bit.

    // the field is obtained by combining the delta value with a base value.
    // The base value depends on the state of the previous value in the following way:
    //  undefined – the base value is the initial value if present in the instruction context. Otherwise a type dependant default base value is used.

    result.as("initial_striABCD");
    BOOST_CHECK( encode_mref("\x80\x83\x84\x41\x42\x43\x44",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80\x83\x84\x41\x42\x43\x44",
                                 ext_cref<unicode_string_cref, delta_operator_tag, optional_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &alloc ) );

    inst.destruct_value(storage, &alloc);
  }
}

BOOST_AUTO_TEST_CASE(operator_tail_ascii_encode_test)
{
  debug_allocator alloc;
  value_storage storage;

  { // testing mandatory field with initial value
    const char* default_value = "initial_string";
    ascii_field_instruction inst(operator_tail,
                                 presence_mandatory,
                                 1,
                                 "test_ascii","",
                                 0,
                                 string_value_storage(default_value));

    inst.construct_value(storage, &alloc);

    ascii_string_mref result(&alloc, &storage, &inst);
    // Mandatory string and byte vector fields – one bit.

    // the field is obtained by combining the delta value with a base value.
    // The base value depends on the state of the previous value in the following way:
    //  undefined – the base value is the initial value if present in the instruction context. Otherwise a type dependant default base value is used.
    result.as("initial_svalue");
    BOOST_CHECK( encode_mref("\xC0\x76\x61\x6C\x75\xE5",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\xC0\x76\x61\x6C\x75\xE5",
                                 ext_cref<ascii_string_cref, tail_operator_tag, mandatory_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &alloc ) );

    inst.destruct_value(storage, &alloc);
  }
  { // testing mandatory field with initial value while tail value not in the stream
    const char* default_value = "initial_string";
    ascii_field_instruction inst(operator_tail,
                                 presence_mandatory,
                                 1,
                                 "test_ascii","",
                                 0,
                                 string_value_storage(default_value));

    inst.construct_value(storage, &alloc);

    ascii_string_mref result(&alloc, &storage, &inst);
    // Mandatory string and byte vector fields – one bit.

    // the field is obtained by combining the delta value with a base value.
    // The base value depends on the state of the previous value in the following way:
    //  undefined – the base value is the initial value if present in the instruction context. Otherwise a type dependant default base value is used.
    result.as("initial_string");
    BOOST_CHECK( encode_mref("\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<ascii_string_cref, tail_operator_tag, mandatory_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &alloc ) );
    ascii_string_mref prev(&alloc, &inst.prev_value(), &inst);

    // change the previous value to "ABCDE" so we can verified the case with defined previous value
    prev = "ABCDE";
    // If the tail value is not present in the stream, the value of the field depends on the state of the previous value in the following way::
    //  assigned – the value of the field is the previous value.
    result.as("ABCDE");
    BOOST_CHECK( encode_mref("\x80",  result,  PRESERVE_PREVIOUS_VALUE ) );
    prev = "ABCDE";
    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<ascii_string_cref, tail_operator_tag, mandatory_with_initial_value_tag>(result),
                                 PRESERVE_PREVIOUS_VALUE, &alloc ) );
    inst.destruct_value(storage, &alloc);

  }
  { // testing mandatory field without initial value while tail value is in the stream

    ascii_field_instruction inst(operator_tail,
                                 presence_mandatory,
                                 1,
                                 "test_ascii","",
                                 0, string_value_storage());

    inst.construct_value(storage, &alloc);
    ascii_string_mref result(&alloc, &storage, &inst);

    // Mandatory string and byte vector fields – one bit.

    // the field is obtained by combining the delta value with a base value.
    // The base value depends on the state of the previous value in the following way:
    //  undefined – the base value is the initial value if present in the instruction context. Otherwise a type dependant default base value is used.
    result.as("value");


    BOOST_CHECK( encode_mref("\xC0\x76\x61\x6C\x75\xE5", result, CHANGE_PREVIOUS_VALUE) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\xC0\x76\x61\x6C\x75\xE5",
                                 ext_cref<ascii_string_cref, tail_operator_tag, mandatory_without_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &alloc) );
    inst.destruct_value(storage, &alloc);

  }
  { // testing optional field with initial value
    const char* default_value = "initial_string";
    ascii_field_instruction inst(operator_tail,
                                 presence_optional,
                                 1,
                                 "test_ascii","",
                                 0,
                                 string_value_storage(default_value));

    inst.construct_value(storage, &alloc);

    ascii_string_mref result(&alloc, &storage, &inst);
    // Optional string and byte vector fields – one bit.
    // The tail value appears in the stream in a nullable representation.
    // A NULL indicates that the value is absent and the state of the previous value is set to empty.

    // the field is obtained by combining the delta value with a base value.
    // The base value depends on the state of the previous value in the following way:
    //  undefined – the base value is the initial value if present in the instruction context. Otherwise a type dependant default base value is used.
    result.as("initial_svalue");
    BOOST_CHECK( encode_mref("\xC0\x76\x61\x6C\x75\xE5",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\xC0\x76\x61\x6C\x75\xE5",
                                 ext_cref<ascii_string_cref, tail_operator_tag, optional_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &alloc) );
    inst.destruct_value(storage, &alloc);
  }

  { // testing optional field with NULL tail value
    const char* default_value = "initial_string";
    ascii_field_instruction inst(operator_tail,
                                 presence_optional,
                                 1,
                                 "test_ascii","",
                                 0,
                                 string_value_storage(default_value));

    inst.construct_value(storage, &alloc);

    ascii_string_mref result(&alloc, &storage, &inst);
    // Optional string and byte vector fields – one bit.
    // The tail value appears in the stream in a nullable representation.
    // A NULL indicates that the value is absent and the state of the previous value is set to empty.

    // the field is obtained by combining the delta value with a base value.
    // The base value depends on the state of the previous value in the following way:
    //  undefined – the base value is the initial value if present in the instruction context. Otherwise a type dependant default base value is used.
    result.omit();
    BOOST_CHECK( encode_mref("\xC0\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\xC0\x80",
                                 ext_cref<ascii_string_cref, tail_operator_tag, optional_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &alloc) );
    inst.destruct_value(storage, &alloc);
  }

  { // testing optional field with initial value while tail value not in the stream
    const char* default_value = "initial_string";
    ascii_field_instruction inst(operator_tail,
                                 presence_optional,
                                 1,
                                 "test_ascii","",
                                 0,
                                 string_value_storage(default_value));

    inst.construct_value(storage, &alloc);

    ascii_string_mref result(&alloc, &storage, &inst);
    // Optional string and byte vector fields – one bit.
    // The tail value appears in the stream in a nullable representation.
    // A NULL indicates that the value is absent and the state of the previous value is set to empty.

    // If the tail value is not present in the stream, the value of the field depends on the state of the previous value in the following way::
    //  undefined – the value of the field is the initial value that also becomes the new previous value..

    result.as("initial_string");
    BOOST_CHECK( encode_mref("\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<ascii_string_cref, tail_operator_tag, optional_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &alloc) );

    ascii_string_mref prev(&alloc, &inst.prev_value(), &inst);
    // change the previous value to "ABCDE" so we can verified the case with defined previous value
    prev = "ABCDE";
    // If the tail value is not present in the stream, the value of the field depends on the state of the previous value in the following way::
    //  assigned – the value of the field is the previous value.
    result.as("ABCDE");
    BOOST_CHECK( encode_mref("\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    prev = "ABCDE";
    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<ascii_string_cref, tail_operator_tag, optional_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &alloc) );
    prev.omit();
    // If the tail value is not present in the stream, the value of the field depends on the state of the previous value in the following way:
    // empty – the value of the field is empty. If the field is optional the value is considered absent.
    result.omit();
    BOOST_CHECK( encode_mref("\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    prev.omit();
    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<ascii_string_cref, tail_operator_tag, optional_with_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &alloc) );
    inst.destruct_value(storage, &alloc);

  }
  { // testing optional field without initial value while tail value not in the stream
    // const char* default_value = "initial_string";

    ascii_field_instruction inst(operator_tail,
                                 presence_optional,
                                 1,
                                 "test_ascii","",
                                 0, string_value_storage());

    inst.construct_value(storage, &alloc);

    ascii_string_mref result(&alloc, &storage, &inst);
    // Optional string and byte vector fields – one bit.
    // The tail value appears in the stream in a nullable representation.
    // A NULL indicates that the value is absent and the state of the previous value is set to empty.

    // If the tail value is not present in the stream, the value of the field depends on the state of the previous value in the following way::
    //  undefined – the value of the field is the initial value that also becomes the new previous value.
    // If the field has optional presence and no initial value, the field is considered absent and the state of the previous value is changed to empty.

    result.omit();
    BOOST_CHECK( encode_mref("\x80",  result,  CHANGE_PREVIOUS_VALUE ) );
    inst.prev_value().defined(false);
    BOOST_CHECK( encode_ext_cref("\x80",
                                 ext_cref<ascii_string_cref, tail_operator_tag, optional_without_initial_value_tag>(result),
                                 CHANGE_PREVIOUS_VALUE, &alloc) );
    inst.destruct_value(storage, &alloc);

  }
}
BOOST_AUTO_TEST_SUITE_END()
