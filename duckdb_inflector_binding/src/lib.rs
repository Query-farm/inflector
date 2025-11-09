use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_uchar};
use std::ptr;

/// --- Transform single string ---
/// Returns a newly allocated C string (caller must free)
fn transform_single<F>(input: *const c_char, f: F) -> *mut c_char
where
    F: Fn(&str) -> String,
{
    if input.is_null() {
        return ptr::null_mut();
    }

    let s = unsafe { CStr::from_ptr(input).to_str().unwrap() };
    CString::new(f(s)).unwrap().into_raw()
}

/// --- Predicate single string ---
/// Returns 1 if true, 0 if false or NULL
fn predicate_single<F>(input: *const c_char, f: F) -> c_uchar
where
    F: Fn(&str) -> bool,
{
    if input.is_null() {
        return 0;
    }

    let s = unsafe { CStr::from_ptr(input).to_str().unwrap() };
    if f(s) { 1 } else { 0 }
}

/// Free a single C string returned by the transform functions
#[no_mangle]
pub extern "C" fn free_c_string(s: *mut c_char) {
    if !s.is_null() {
        unsafe {
            drop(CString::from_raw(s));
        }
    }
}

fn to_lower_case(s: &str) -> String {
    s.to_lowercase()
}

fn to_upper_case(s: &str) -> String {
    s.to_uppercase()
}

// --- Transform wrappers ---
#[no_mangle]
pub extern "C" fn cruet_to_class_case(s: *const c_char) -> *mut c_char {
    transform_single(s, cruet::to_class_case)
}
#[no_mangle]
pub extern "C" fn cruet_to_camel_case(s: *const c_char) -> *mut c_char {
    transform_single(s, cruet::to_camel_case)
}
#[no_mangle]
pub extern "C" fn cruet_to_pascal_case(s: *const c_char) -> *mut c_char {
    transform_single(s, cruet::to_pascal_case)
}
#[no_mangle]
pub extern "C" fn cruet_to_screamingsnake_case(s: *const c_char) -> *mut c_char {
    transform_single(s, cruet::to_screaming_snake_case)
}
#[no_mangle]
pub extern "C" fn cruet_to_snake_case(s: *const c_char) -> *mut c_char {
    transform_single(s, cruet::to_snake_case)
}
#[no_mangle]
pub extern "C" fn cruet_to_kebab_case(s: *const c_char) -> *mut c_char {
    transform_single(s, cruet::to_kebab_case)
}
#[no_mangle]
pub extern "C" fn cruet_to_train_case(s: *const c_char) -> *mut c_char {
    transform_single(s, cruet::to_train_case)
}
#[no_mangle]
pub extern "C" fn cruet_to_sentence_case(s: *const c_char) -> *mut c_char {
    transform_single(s, cruet::to_sentence_case)
}
#[no_mangle]
pub extern "C" fn cruet_to_title_case(s: *const c_char) -> *mut c_char {
    transform_single(s, cruet::to_title_case)
}
#[no_mangle]
pub extern "C" fn cruet_to_lower_case(s: *const c_char) -> *mut c_char {
    transform_single(s, to_lower_case)
}
#[no_mangle]
pub extern "C" fn cruet_to_upper_case(s: *const c_char) -> *mut c_char {
    transform_single(s, to_upper_case)
}

#[no_mangle]
pub extern "C" fn cruet_to_table_case(s: *const c_char) -> *mut c_char {
    transform_single(s, cruet::to_table_case)
}
#[no_mangle]
pub extern "C" fn cruet_ordinalize(s: *const c_char) -> *mut c_char {
    transform_single(s, cruet::ordinalize)
}
#[no_mangle]
pub extern "C" fn cruet_deordinalize(s: *const c_char) -> *mut c_char {
    transform_single(s, cruet::deordinalize)
}
#[no_mangle]
pub extern "C" fn cruet_to_foreign_key(s: *const c_char) -> *mut c_char {
    transform_single(s, cruet::to_foreign_key)
}
#[no_mangle]
pub extern "C" fn cruet_demodulize(s: *const c_char) -> *mut c_char {
    transform_single(s, cruet::demodulize)
}
#[no_mangle]
pub extern "C" fn cruet_deconstantize(s: *const c_char) -> *mut c_char {
    transform_single(s, cruet::deconstantize)
}
#[no_mangle]
pub extern "C" fn cruet_to_plural(s: *const c_char) -> *mut c_char {
    transform_single(s, cruet::to_plural)
}
#[no_mangle]
pub extern "C" fn cruet_to_singular(s: *const c_char) -> *mut c_char {
    transform_single(s, cruet::to_singular)
}

// --- Predicate wrappers ---
#[no_mangle]
pub extern "C" fn cruet_is_class_case(s: *const c_char) -> c_uchar {
    predicate_single(s, cruet::is_class_case)
}
#[no_mangle]
pub extern "C" fn cruet_is_camel_case(s: *const c_char) -> c_uchar {
    predicate_single(s, cruet::is_camel_case)
}
#[no_mangle]
pub extern "C" fn cruet_is_pascal_case(s: *const c_char) -> c_uchar {
    predicate_single(s, cruet::is_pascal_case)
}
#[no_mangle]
pub extern "C" fn cruet_is_screamingsnake_case(s: *const c_char) -> c_uchar {
    predicate_single(s, cruet::is_screaming_snake_case)
}
#[no_mangle]
pub extern "C" fn cruet_is_snake_case(s: *const c_char) -> c_uchar {
    predicate_single(s, cruet::is_snake_case)
}
#[no_mangle]
pub extern "C" fn cruet_is_kebab_case(s: *const c_char) -> c_uchar {
    predicate_single(s, cruet::is_kebab_case)
}
#[no_mangle]
pub extern "C" fn cruet_is_train_case(s: *const c_char) -> c_uchar {
    predicate_single(s, cruet::is_train_case)
}
#[no_mangle]
pub extern "C" fn cruet_is_sentence_case(s: *const c_char) -> c_uchar {
    predicate_single(s, cruet::is_sentence_case)
}
#[no_mangle]
pub extern "C" fn cruet_is_title_case(s: *const c_char) -> c_uchar {
    predicate_single(s, cruet::is_title_case)
}
#[no_mangle]
pub extern "C" fn cruet_is_table_case(s: *const c_char) -> c_uchar {
    predicate_single(s, cruet::is_table_case)
}
#[no_mangle]
pub extern "C" fn cruet_is_foreign_key(s: *const c_char) -> c_uchar {
    predicate_single(s, cruet::is_foreign_key)
}
