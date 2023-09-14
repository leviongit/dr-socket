#include "serialize.h"
#include "help.h"
#include "api.h"
#include <lyniat/memory.h>

namespace lyniat::socket::serialize {

    serialized_type get_st(mrb_value data){
        if(mrb_nil_p(data)){
            return ST_NIL;
        }
        mrb_vtype type = mrb_type(data);
        switch (type) {
            case MRB_TT_FALSE:
                return ST_FALSE;
            case MRB_TT_TRUE:
                return ST_TRUE;
            case MRB_TT_STRING:
                return ST_STRING;
            case MRB_TT_INTEGER:
                return ST_INT;
            case MRB_TT_FLOAT:
                return ST_FLOAT;
            case MRB_TT_SYMBOL:
                return ST_SYMBOL;
            case MRB_TT_HASH:
                return ST_HASH;
            case MRB_TT_ARRAY:
                return ST_ARRAY;
            default:
                return ST_UNDEF;
        }
    }

    void serialize_data(buffer::BinaryBuffer *binary_buffer, mrb_state *state, mrb_value data) {
        auto stype = get_st(data);
        auto type = (unsigned char)stype;
        if(stype == ST_FALSE || stype == ST_TRUE || stype == ST_NIL) {
            binary_buffer->Append(type);
        }

        else if(stype == ST_INT){
            mrb_int number = cext_to_int(state, data);
            binary_buffer->Append(type);
            binary_buffer->Append(number);
        }

        else if(stype == ST_FLOAT){
            mrb_float number = cext_to_float(state, data);
            binary_buffer->Append(type);
            binary_buffer->Append(number);
        }

        else if(stype == ST_STRING){
            const char *string = cext_to_string(state, data);
            st_counter_t str_len = strlen(string) + 1;
            binary_buffer->Append(type);
            binary_buffer->Append(str_len);
            binary_buffer->Append((void*)string, str_len);
        }

        else if(stype == ST_SYMBOL){
            const char *string = API->mrb_sym_name(state, API->mrb_obj_to_sym(state, data));
            st_counter_t str_len = strlen(string) + 1;
            binary_buffer->Append(type);
            binary_buffer->Append(str_len);
            binary_buffer->Append((void*)string, str_len);
        }

        else if(stype == ST_ARRAY){
            binary_buffer->Append(type);
            auto current_pos = binary_buffer->CurrentPos();
            binary_buffer->Append((st_counter_t)0); // array_size
            st_counter_t array_size = 0;
            for (mrb_int i = 0; i < RARRAY_LEN(data); i++) {
                auto object = RARRAY_PTR(data)[i];
                serialize_data(binary_buffer, state, object);
                array_size++;
            }
            binary_buffer->SetAt(current_pos, array_size);
        }

        else if (stype == ST_HASH) {
            binary_buffer->Append(type);
            auto current_pos = binary_buffer->CurrentPos();
            binary_buffer->Append((st_counter_t)0); // hash_size
            st_counter_t hash_size = 0;
            auto hash = mrb_hash_ptr(data);

            typedef struct to_pass_t {buffer::BinaryBuffer *buffer; st_counter_t *counter;} to_pass_t;
            to_pass_t to_pass = {binary_buffer, &hash_size};

            API->mrb_hash_foreach(state, hash, {[](mrb_state *intern_state, mrb_value key, mrb_value val, void* passed) -> int {
                auto to_pass = (to_pass_t*)passed;
                auto binary_buffer = to_pass->buffer;
                st_counter_t *hash_size = to_pass->counter;
                const char *s_key;
                serialized_type key_type;
                if(cext_is_symbol(intern_state, key)){
                    s_key = API->mrb_sym_name(intern_state, API->mrb_obj_to_sym(intern_state, key));
                    key_type = ST_SYMBOL;
                } else if (cext_is_string(intern_state, key)){
                    s_key = API->mrb_string_cstr(intern_state, key);
                    key_type = ST_STRING;
                } else {
                    return 0;
                }

                binary_buffer->Append(key_type);
                st_counter_t str_len = strlen(s_key) + 1;
                binary_buffer->Append(str_len);
                binary_buffer->Append((void*)s_key, str_len);

                serialize_data(binary_buffer, intern_state, val);
                *hash_size += 1;
                return 0;
            }}, &to_pass);

            binary_buffer->SetAt(current_pos, hash_size);
        }
    }

    mrb_value deserialize_data(buffer::BinaryBuffer *binary_buffer, mrb_state *state) {
        unsigned char bin_type;
        binary_buffer->Read(&bin_type);
        auto type = (serialized_type)bin_type;
        if (type == ST_FALSE) {
            return mrb_false_value();
        }
        else if (type == ST_TRUE) {
            return mrb_true_value();
        }
        else if (type == ST_NIL) {
            return mrb_nil_value();
        }
        else if (type == ST_STRING) {
            st_counter_t data_size;
            binary_buffer->Read(&data_size);
            auto str_ptr = MALLOC_CYCLE(data_size);
            binary_buffer->Read(str_ptr, data_size);
            mrb_value data = API->mrb_str_new(state, (const char*)str_ptr, data_size - 1);
            return data;
        }
        else if (type == ST_SYMBOL) {
            st_counter_t data_size;
            binary_buffer->Read(&data_size);
            auto str_ptr = MALLOC_CYCLE(data_size);
            binary_buffer->Read(str_ptr, data_size);
            mrb_value data = API->mrb_symbol_value(API->mrb_intern_check(state, (const char*)str_ptr, data_size - 1));
            return data;
        }
        else if (type == ST_INT) {
            mrb_int num;
            binary_buffer->Read(&num);
            return API->mrb_int_value(state, num);
        }
        else if (type == ST_FLOAT) {
            mrb_float num;
            binary_buffer->Read(&num);
            return API->mrb_float_value(state, num);
        }
        else if (type == ST_HASH) {
            st_counter_t hash_size;
            binary_buffer->Read(&hash_size);

            mrb_value hash = API->mrb_hash_new(state);

            for (st_counter_t i = 0; i < hash_size; ++i) {
                //key
                unsigned char key_type;
                binary_buffer->Read(&key_type);
                st_counter_t key_size;
                binary_buffer->Read(&key_size);
                auto str_ptr = MALLOC_CYCLE(key_size);
                binary_buffer->Read(str_ptr, key_size);

                mrb_value data = deserialize_data(binary_buffer, state);

                if (key_type == ST_STRING) {
                    cext_hash_set_kstr(state, hash, (const char *) str_ptr, data);
                } else if (key_type == ST_SYMBOL) {
                    cext_hash_set_ksym(state, hash, (const char *) str_ptr, data);
                }
            }
            return hash;
        }

        else if (type == ST_ARRAY) {
            st_counter_t array_size;
            binary_buffer->Read(&array_size);

            mrb_value array = API->mrb_ary_new(state);

            for (st_counter_t i = 0; i < array_size; ++i) {
                mrb_value data = deserialize_data(binary_buffer, state);
                API->mrb_ary_push(state, array, data);
            }
            return array;
        }

        return mrb_nil_value();
    }
}