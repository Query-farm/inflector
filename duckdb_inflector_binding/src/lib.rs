use std::collections::HashSet;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_uchar};
use std::ptr;
use std::sync::{OnceLock, RwLock};

use convert_case::{Case, Casing};

// --- Global acronym storage ---

fn acronyms() -> &'static RwLock<HashSet<String>> {
    static ACRONYMS: OnceLock<RwLock<HashSet<String>>> = OnceLock::new();
    ACRONYMS.get_or_init(|| RwLock::new(HashSet::new()))
}

/// Set acronyms from a comma-separated string. Tokens are uppercased.
/// Single-character tokens are silently ignored (minimum 2 chars).
#[no_mangle]
pub extern "C" fn cruet_set_acronyms(csv: *const c_char) {
    if csv.is_null() {
        return;
    }
    let s = unsafe { CStr::from_ptr(csv).to_str().unwrap() };
    let mut set = HashSet::new();
    for token in s.split(',') {
        let trimmed = token.trim().to_uppercase();
        if trimmed.len() >= 2 {
            set.insert(trimmed);
        }
    }
    *acronyms().write().unwrap() = set;
}

/// Get the current acronyms as a sorted comma-separated string.
/// Caller must free the returned string with `free_c_string`.
#[no_mangle]
pub extern "C" fn cruet_get_acronyms() -> *mut c_char {
    let set = acronyms().read().unwrap();
    let mut sorted: Vec<&String> = set.iter().collect();
    sorted.sort();
    let csv = sorted
        .into_iter()
        .map(|s| s.as_str())
        .collect::<Vec<_>>()
        .join(",");
    CString::new(csv).unwrap().into_raw()
}

/// Clear all configured acronyms, restoring default behavior.
#[no_mangle]
pub extern "C" fn cruet_clear_acronyms() {
    acronyms().write().unwrap().clear();
}

// --- Acronym-aware case conversion ---

fn capitalize(s: &str) -> String {
    let mut chars = s.chars();
    match chars.next() {
        None => String::new(),
        Some(c) => {
            let mut result = c.to_uppercase().to_string();
            result.push_str(chars.as_str());
            result
        }
    }
}

fn convert_with_acronyms(input: &str, case: Case) -> String {
    let acros = acronyms().read().unwrap();

    if acros.is_empty() {
        return input.to_case(case);
    }

    match case {
        // Acronyms don't affect all-lowercase or all-uppercase output
        Case::Snake | Case::Kebab | Case::Lower | Case::UpperSnake | Case::Upper | Case::Flat
        | Case::UpperFlat => input.to_case(case),

        Case::Pascal => {
            let snake = input.to_case(Case::Snake);
            snake
                .split('_')
                .map(|w| {
                    let upper = w.to_uppercase();
                    if acros.contains(&upper) {
                        upper
                    } else {
                        capitalize(w)
                    }
                })
                .collect::<Vec<_>>()
                .join("")
        }

        Case::Camel => {
            let snake = input.to_case(Case::Snake);
            snake
                .split('_')
                .enumerate()
                .map(|(i, w)| {
                    if i == 0 {
                        w.to_lowercase()
                    } else {
                        let upper = w.to_uppercase();
                        if acros.contains(&upper) {
                            upper
                        } else {
                            capitalize(w)
                        }
                    }
                })
                .collect::<Vec<_>>()
                .join("")
        }

        Case::Title => {
            let snake = input.to_case(Case::Snake);
            snake
                .split('_')
                .map(|w| {
                    let upper = w.to_uppercase();
                    if acros.contains(&upper) {
                        upper
                    } else {
                        capitalize(w)
                    }
                })
                .collect::<Vec<_>>()
                .join(" ")
        }

        Case::Train => {
            let snake = input.to_case(Case::Snake);
            snake
                .split('_')
                .map(|w| {
                    let upper = w.to_uppercase();
                    if acros.contains(&upper) {
                        upper
                    } else {
                        capitalize(w)
                    }
                })
                .collect::<Vec<_>>()
                .join("-")
        }

        Case::Sentence => {
            let snake = input.to_case(Case::Snake);
            snake
                .split('_')
                .enumerate()
                .map(|(i, w)| {
                    let upper = w.to_uppercase();
                    if acros.contains(&upper) {
                        upper
                    } else if i == 0 {
                        capitalize(w)
                    } else {
                        w.to_lowercase()
                    }
                })
                .collect::<Vec<_>>()
                .join(" ")
        }

        // Default fallback for any other cases
        _ => input.to_case(case),
    }
}

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

fn to_table_case(s: &str) -> String {
    let snake = s.to_case(Case::Snake);
    cruet::to_plural(&snake)
}

fn to_foreign_key(s: &str) -> String {
    let snake = s.to_case(Case::Snake);
    if snake.ends_with("_id") {
        snake
    } else {
        format!("{}_id", snake)
    }
}

// --- Transform wrappers ---
#[no_mangle]
pub extern "C" fn cruet_to_class_case(s: *const c_char) -> *mut c_char {
    transform_single(s, |s| convert_with_acronyms(s, Case::Pascal))
}
#[no_mangle]
pub extern "C" fn cruet_to_camel_case(s: *const c_char) -> *mut c_char {
    transform_single(s, |s| convert_with_acronyms(s, Case::Camel))
}
#[no_mangle]
pub extern "C" fn cruet_to_pascal_case(s: *const c_char) -> *mut c_char {
    transform_single(s, |s| convert_with_acronyms(s, Case::Pascal))
}
#[no_mangle]
pub extern "C" fn cruet_to_screamingsnake_case(s: *const c_char) -> *mut c_char {
    transform_single(s, |s| convert_with_acronyms(s, Case::UpperSnake))
}
#[no_mangle]
pub extern "C" fn cruet_to_snake_case(s: *const c_char) -> *mut c_char {
    transform_single(s, |s| convert_with_acronyms(s, Case::Snake))
}
#[no_mangle]
pub extern "C" fn cruet_to_kebab_case(s: *const c_char) -> *mut c_char {
    transform_single(s, |s| convert_with_acronyms(s, Case::Kebab))
}
#[no_mangle]
pub extern "C" fn cruet_to_train_case(s: *const c_char) -> *mut c_char {
    transform_single(s, |s| convert_with_acronyms(s, Case::Train))
}
#[no_mangle]
pub extern "C" fn cruet_to_sentence_case(s: *const c_char) -> *mut c_char {
    transform_single(s, |s| convert_with_acronyms(s, Case::Sentence))
}
#[no_mangle]
pub extern "C" fn cruet_to_title_case(s: *const c_char) -> *mut c_char {
    transform_single(s, |s| convert_with_acronyms(s, Case::Title))
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
    transform_single(s, to_table_case)
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
    transform_single(s, to_foreign_key)
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
    predicate_single(s, |s| convert_with_acronyms(s, Case::Pascal) == s)
}
#[no_mangle]
pub extern "C" fn cruet_is_camel_case(s: *const c_char) -> c_uchar {
    predicate_single(s, |s| convert_with_acronyms(s, Case::Camel) == s)
}
#[no_mangle]
pub extern "C" fn cruet_is_pascal_case(s: *const c_char) -> c_uchar {
    predicate_single(s, |s| convert_with_acronyms(s, Case::Pascal) == s)
}
#[no_mangle]
pub extern "C" fn cruet_is_screamingsnake_case(s: *const c_char) -> c_uchar {
    predicate_single(s, |s| convert_with_acronyms(s, Case::UpperSnake) == s)
}
#[no_mangle]
pub extern "C" fn cruet_is_snake_case(s: *const c_char) -> c_uchar {
    predicate_single(s, |s| convert_with_acronyms(s, Case::Snake) == s)
}
#[no_mangle]
pub extern "C" fn cruet_is_kebab_case(s: *const c_char) -> c_uchar {
    predicate_single(s, |s| convert_with_acronyms(s, Case::Kebab) == s)
}
#[no_mangle]
pub extern "C" fn cruet_is_train_case(s: *const c_char) -> c_uchar {
    predicate_single(s, |s| convert_with_acronyms(s, Case::Train) == s)
}
#[no_mangle]
pub extern "C" fn cruet_is_sentence_case(s: *const c_char) -> c_uchar {
    predicate_single(s, |s| convert_with_acronyms(s, Case::Sentence) == s)
}
#[no_mangle]
pub extern "C" fn cruet_is_title_case(s: *const c_char) -> c_uchar {
    predicate_single(s, |s| convert_with_acronyms(s, Case::Title) == s)
}
#[no_mangle]
pub extern "C" fn cruet_is_table_case(s: *const c_char) -> c_uchar {
    predicate_single(s, |s| to_table_case(s) == s)
}
#[no_mangle]
pub extern "C" fn cruet_is_foreign_key(s: *const c_char) -> c_uchar {
    predicate_single(s, |s| to_foreign_key(s) == s)
}
