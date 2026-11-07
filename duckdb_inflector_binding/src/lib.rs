//! FFI bindings between the DuckDB inflector extension (C++) and the `cruet` /
//! `convert_case` crates.
//!
//! Design notes:
//! * Strings cross the boundary as (pointer, length) pairs, so embedded NUL
//!   bytes are handled and no C-string scanning is needed.
//! * There is no global state. The `inflector_acronyms` setting is
//!   session-scoped in DuckDB, so the C++ side reads it from the ClientContext
//!   and passes an `InflectorAcronyms` handle into every call.
//! * Every `extern "C"` function is wrapped in `catch_unwind`; a panic is
//!   reported as `InflectorStatus::Panic` instead of unwinding into C++.

use std::collections::HashSet;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

use convert_case::{Case, Casing};

/// The operation performed by `inflector_transform` / `inflector_predicate`.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum InflectorOp {
    ClassCase = 0,
    CamelCase = 1,
    PascalCase = 2,
    ScreamingSnakeCase = 3,
    SnakeCase = 4,
    KebabCase = 5,
    TrainCase = 6,
    SentenceCase = 7,
    TitleCase = 8,
    LowerCase = 9,
    UpperCase = 10,
    TableCase = 11,
    ForeignKey = 12,
    Demodulize = 13,
    Deconstantize = 14,
    Ordinalize = 15,
    Deordinalize = 16,
    Plural = 17,
    Singular = 18,
    Humanize = 19,
    Parameterize = 20,
    Classify = 21,
}

/// Result status of an FFI call.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum InflectorStatus {
    Ok = 0,
    /// The input was not valid UTF-8.
    InvalidUtf8 = 1,
    /// A required pointer argument was NULL.
    NullArgument = 2,
    /// The Rust code panicked (a bug); the panic was caught at the boundary.
    Panic = 3,
}

/// An immutable set of acronyms (uppercased, at least two bytes long).
pub struct InflectorAcronyms {
    set: HashSet<String>,
}

/// Borrow `len` bytes at `data` as `&str`. A zero length never dereferences `data`.
unsafe fn borrow_str<'a>(data: *const u8, len: usize) -> Result<&'a str, InflectorStatus> {
    if len == 0 {
        return Ok("");
    }
    if data.is_null() {
        return Err(InflectorStatus::NullArgument);
    }
    let bytes = std::slice::from_raw_parts(data, len);
    std::str::from_utf8(bytes).map_err(|_| InflectorStatus::InvalidUtf8)
}

fn parse_acronyms(s: &str) -> HashSet<String> {
    s.split(',')
        .map(|token| token.trim().to_uppercase())
        .filter(|token| token.len() >= 2)
        .collect()
}

/// Build an acronym set from a comma-separated list. Tokens are trimmed and
/// uppercased; tokens shorter than two bytes are ignored.
/// Returns NULL on invalid UTF-8 or internal error. Free with `inflector_acronyms_free`.
///
/// # Safety
/// Pointer arguments must be NULL or valid for the stated lengths.
#[no_mangle]
pub unsafe extern "C" fn inflector_acronyms_new(
    csv: *const u8,
    len: usize,
) -> *mut InflectorAcronyms {
    catch_unwind(|| {
        let s = match unsafe { borrow_str(csv, len) } {
            Ok(s) => s,
            Err(_) => return ptr::null_mut(),
        };
        Box::into_raw(Box::new(InflectorAcronyms {
            set: parse_acronyms(s),
        }))
    })
    .unwrap_or(ptr::null_mut())
}

/// Free an acronym set created by `inflector_acronyms_new`. NULL is a no-op.
///
/// # Safety
/// Pointer arguments must be NULL or valid for the stated lengths.
#[no_mangle]
pub unsafe extern "C" fn inflector_acronyms_free(acronyms: *mut InflectorAcronyms) {
    if acronyms.is_null() {
        return;
    }
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        drop(Box::from_raw(acronyms));
    }));
}

/// Apply `op` to the UTF-8 string (`input`, `input_len`). `acronyms` may be NULL
/// (no acronyms). On `Ok`, `*out_data`/`*out_len` receive a newly allocated
/// buffer (not NUL-terminated) that must be released with `inflector_free_buffer`.
/// On any other status nothing is allocated and the outputs are set to NULL/0.
///
/// # Safety
/// Pointer arguments must be NULL or valid for the stated lengths.
#[no_mangle]
pub unsafe extern "C" fn inflector_transform(
    op: InflectorOp,
    input: *const u8,
    input_len: usize,
    acronyms: *const InflectorAcronyms,
    out_data: *mut *mut u8,
    out_len: *mut usize,
) -> InflectorStatus {
    if out_data.is_null() || out_len.is_null() {
        return InflectorStatus::NullArgument;
    }
    unsafe {
        *out_data = ptr::null_mut();
        *out_len = 0;
    }
    let result = catch_unwind(AssertUnwindSafe(|| -> Result<Vec<u8>, InflectorStatus> {
        let s = unsafe { borrow_str(input, input_len) }?;
        let acros = acronym_set(acronyms);
        Ok(apply(op, s, acros).into_bytes())
    }));
    match result {
        Ok(Ok(bytes)) => {
            let boxed = bytes.into_boxed_slice();
            let len = boxed.len();
            let raw = Box::into_raw(boxed) as *mut u8;
            unsafe {
                *out_data = raw;
                *out_len = len;
            }
            InflectorStatus::Ok
        }
        Ok(Err(status)) => status,
        Err(_) => InflectorStatus::Panic,
    }
}

/// Returns (in `*out`) whether applying `op` to the input leaves it unchanged,
/// i.e. whether the input is already in the form `op` produces.
///
/// # Safety
/// Pointer arguments must be NULL or valid for the stated lengths.
#[no_mangle]
pub unsafe extern "C" fn inflector_predicate(
    op: InflectorOp,
    input: *const u8,
    input_len: usize,
    acronyms: *const InflectorAcronyms,
    out: *mut bool,
) -> InflectorStatus {
    if out.is_null() {
        return InflectorStatus::NullArgument;
    }
    unsafe {
        *out = false;
    }
    let result = catch_unwind(AssertUnwindSafe(|| -> Result<bool, InflectorStatus> {
        let s = unsafe { borrow_str(input, input_len) }?;
        let acros = acronym_set(acronyms);
        Ok(apply(op, s, acros) == s)
    }));
    match result {
        Ok(Ok(value)) => {
            unsafe {
                *out = value;
            }
            InflectorStatus::Ok
        }
        Ok(Err(status)) => status,
        Err(_) => InflectorStatus::Panic,
    }
}

/// Free a buffer returned by `inflector_transform`. NULL is a no-op.
///
/// # Safety
/// Pointer arguments must be NULL or valid for the stated lengths.
#[no_mangle]
pub unsafe extern "C" fn inflector_free_buffer(data: *mut u8, len: usize) {
    if data.is_null() {
        return;
    }
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        drop(Box::from_raw(ptr::slice_from_raw_parts_mut(data, len)));
    }));
}

fn empty_set() -> &'static HashSet<String> {
    static EMPTY: std::sync::OnceLock<HashSet<String>> = std::sync::OnceLock::new();
    EMPTY.get_or_init(HashSet::new)
}

fn acronym_set<'a>(acronyms: *const InflectorAcronyms) -> &'a HashSet<String> {
    if acronyms.is_null() {
        empty_set()
    } else {
        unsafe { &(*acronyms).set }
    }
}

fn apply(op: InflectorOp, s: &str, acros: &HashSet<String>) -> String {
    match op {
        InflectorOp::ClassCase | InflectorOp::PascalCase => {
            convert_with_acronyms(s, Case::Pascal, acros)
        }
        InflectorOp::CamelCase => convert_with_acronyms(s, Case::Camel, acros),
        InflectorOp::ScreamingSnakeCase => convert_with_acronyms(s, Case::UpperSnake, acros),
        InflectorOp::SnakeCase => convert_with_acronyms(s, Case::Snake, acros),
        InflectorOp::KebabCase => convert_with_acronyms(s, Case::Kebab, acros),
        InflectorOp::TrainCase => convert_with_acronyms(s, Case::Train, acros),
        InflectorOp::SentenceCase => convert_with_acronyms(s, Case::Sentence, acros),
        InflectorOp::TitleCase => convert_with_acronyms(s, Case::Title, acros),
        InflectorOp::LowerCase => s.to_lowercase(),
        InflectorOp::UpperCase => s.to_uppercase(),
        InflectorOp::TableCase => to_table_case(s),
        InflectorOp::ForeignKey => to_foreign_key(s),
        InflectorOp::Demodulize => cruet::demodulize(s),
        InflectorOp::Deconstantize => cruet::deconstantize(s),
        InflectorOp::Ordinalize => cruet::ordinalize(s),
        InflectorOp::Deordinalize => cruet::deordinalize(s),
        InflectorOp::Plural => cruet::to_plural(s),
        InflectorOp::Singular => to_singular(s),
        InflectorOp::Humanize => humanize(s, acros),
        InflectorOp::Parameterize => parameterize(s),
        InflectorOp::Classify => classify(s, acros),
    }
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

fn convert_with_acronyms(input: &str, case: Case, acros: &HashSet<String>) -> String {
    if acros.is_empty() {
        return input.to_case(case);
    }

    let word = |w: &str, fallback: fn(&str) -> String| -> String {
        let upper = w.to_uppercase();
        if acros.contains(&upper) {
            upper
        } else {
            fallback(w)
        }
    };

    match case {
        Case::Pascal => {
            let snake = input.to_case(Case::Snake);
            snake
                .split('_')
                .map(|w| word(w, capitalize))
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
                        word(w, capitalize)
                    }
                })
                .collect::<Vec<_>>()
                .join("")
        }
        Case::Title => {
            let snake = input.to_case(Case::Snake);
            snake
                .split('_')
                .map(|w| word(w, capitalize))
                .collect::<Vec<_>>()
                .join(" ")
        }
        Case::Train => {
            let snake = input.to_case(Case::Snake);
            snake
                .split('_')
                .map(|w| word(w, capitalize))
                .collect::<Vec<_>>()
                .join("-")
        }
        Case::Sentence => {
            let snake = input.to_case(Case::Snake);
            snake
                .split('_')
                .enumerate()
                .map(|(i, w)| {
                    if i == 0 {
                        word(w, capitalize)
                    } else {
                        word(w, |w| w.to_lowercase())
                    }
                })
                .collect::<Vec<_>>()
                .join(" ")
        }
        // Acronyms don't affect all-lowercase or all-uppercase output
        _ => input.to_case(case),
    }
}

fn to_singular(s: &str) -> String {
    // cruet pluralizes person/child (including compound suffixes), but has no
    // inverse rules for people/children. Preserve the prefix and the
    // conventional lower, title, or upper case of these irregular suffixes.
    for (plural, singular) in [
        ("people", "person"),
        ("People", "Person"),
        ("PEOPLE", "PERSON"),
        ("children", "child"),
        ("Children", "Child"),
        ("CHILDREN", "CHILD"),
    ] {
        if let Some(prefix) = s.strip_suffix(plural) {
            return format!("{prefix}{singular}");
        }
    }
    cruet::to_singular(s)
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

/// Rails-style humanize: drop a trailing `_id`, then render as Sentence case
/// ("employee_salary" -> "Employee salary", "author_id" -> "Author").
fn humanize(s: &str, acros: &HashSet<String>) -> String {
    let trimmed = s.trim();
    let base = match trimmed.strip_suffix("_id") {
        Some(rest) if !rest.trim_matches('_').is_empty() => rest,
        _ => trimmed,
    };
    capitalize(convert_with_acronyms(base, Case::Sentence, acros).trim())
}

/// URL slug: lowercase, every run of non-alphanumeric characters becomes a
/// single '-', no leading or trailing '-'. Unicode letters and digits are kept
/// (the C++ side strips accents before calling this).
fn parameterize(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut pending_sep = false;
    for c in s.chars() {
        if c.is_alphanumeric() {
            if pending_sep && !out.is_empty() {
                out.push('-');
            }
            pending_sep = false;
            out.extend(c.to_lowercase());
        } else {
            pending_sep = true;
        }
    }
    out
}

/// Rails-style classify: singularize the (snake_case) table name and render it
/// in PascalCase ("egg_and_hams" -> "EggAndHam", "people" -> "Person").
fn classify(s: &str, acros: &HashSet<String>) -> String {
    let snake = s.to_case(Case::Snake);
    convert_with_acronyms(&to_singular(&snake), Case::Pascal, acros)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn run(op: InflectorOp, s: &str, acros: *const InflectorAcronyms) -> (InflectorStatus, String) {
        let mut data: *mut u8 = ptr::null_mut();
        let mut len: usize = 0;
        let status =
            unsafe { inflector_transform(op, s.as_ptr(), s.len(), acros, &mut data, &mut len) };
        let out = if status == InflectorStatus::Ok {
            let v = unsafe { std::slice::from_raw_parts(data, len) }.to_vec();
            unsafe { inflector_free_buffer(data, len) };
            String::from_utf8(v).unwrap()
        } else {
            String::new()
        };
        (status, out)
    }

    #[test]
    fn basic_ops() {
        assert_eq!(
            run(InflectorOp::SnakeCase, "helloWorld", ptr::null()).1,
            "hello_world"
        );
        assert_eq!(
            run(InflectorOp::Humanize, "author_id", ptr::null()).1,
            "Author"
        );
        assert_eq!(
            run(InflectorOp::Humanize, "employee_salary", ptr::null()).1,
            "Employee salary"
        );
        assert_eq!(
            run(
                InflectorOp::Parameterize,
                "  Donald E. Knuth!! ",
                ptr::null()
            )
            .1,
            "donald-e-knuth"
        );
        assert_eq!(
            run(InflectorOp::Classify, "egg_and_hams", ptr::null()).1,
            "EggAndHam"
        );
        assert_eq!(
            run(InflectorOp::Classify, "people", ptr::null()).1,
            "Person"
        );
        assert_eq!(run(InflectorOp::SnakeCase, "", ptr::null()).1, "");
    }

    #[test]
    fn acronyms_are_per_handle() {
        let csv = "html, api";
        let a = unsafe { inflector_acronyms_new(csv.as_ptr(), csv.len()) };
        assert_eq!(
            run(InflectorOp::PascalCase, "html_parser", a).1,
            "HTMLParser"
        );
        assert_eq!(
            run(InflectorOp::PascalCase, "html_parser", ptr::null()).1,
            "HtmlParser"
        );
        unsafe { inflector_acronyms_free(a) };
    }

    #[test]
    fn invalid_utf8_and_nul() {
        let bad = [0x66u8, 0xff, 0x6f];
        let mut data: *mut u8 = ptr::null_mut();
        let mut len = 0usize;
        let st = unsafe {
            inflector_transform(
                InflectorOp::SnakeCase,
                bad.as_ptr(),
                bad.len(),
                ptr::null(),
                &mut data,
                &mut len,
            )
        };
        assert_eq!(st, InflectorStatus::InvalidUtf8);
        assert!(data.is_null());
        let (st, out) = run(InflectorOp::UpperCase, "a\0b", ptr::null());
        assert_eq!(st, InflectorStatus::Ok);
        assert_eq!(out, "A\0B");
    }
}
