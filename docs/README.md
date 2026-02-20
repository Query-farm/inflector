# DuckDB Inflector Extension by [Query.Farm](https://query.farm)

The **Inflector** extension, developed by **[Query.Farm](https://query.farm)**, brings powerful string case transformation and inflection capabilities directly to your SQL queries in DuckDB. Transform column names, validate naming conventions, handle pluralization, and more—all without leaving your database environment.

Whether you're normalizing API responses, migrating schemas between naming conventions, or ensuring consistent data formatting, Inflector makes string manipulation in SQL simple and efficient.

## Use Cases

The Inflector extension is perfect for:

- **Data normalization**: Standardize column names and values to a consistent case style
- **ETL pipelines**: Transform and clean data during ingestion or export
- **API and report generation**: Match naming conventions for downstream systems
- **Schema migration**: Convert between naming conventions (snake_case, camelCase, etc.)
- **Data validation**: Check if strings conform to required case or format
- **Pluralization/Singularization**: Automatically convert between singular and plural forms
- **Foreign key and module name handling**: Generate or validate foreign key names and Ruby-style module paths

## Installation

**`inflector` is a [DuckDB Community Extension](https://github.com/duckdb/community-extensions).**

You can install and load it with:

```sql
INSTALL inflector FROM community;
LOAD inflector;
```

## Quick Start

```sql
-- Install and load the extension
INSTALL inflector FROM community;
LOAD inflector;

-- Transform a single string
SELECT inflector_to_camel_case('hello_world');  -- helloWorld

-- Transform a single string
SELECT inflect('camel', 'hello_world');  -- helloWorld

-- Transform all column names in a struct
SELECT inflect('snake', {'firstName': 'John', 'lastName': 'Doe'});
-- {'first_name': John, 'last_name': Doe}

-- Transform all column names from a table or query
SELECT * FROM inflect('kebab', (select * FROM read_csv('data.csv')));
```

## Functions

### Case Transformation Functions

Transform a string to a specific case style:

- `inflector_to_class_case(str)` → `ClassCase`
- `inflector_to_camel_case(str)` → `camelCase`
- `inflector_to_pascal_case(str)` → `PascalCase`
- `inflector_to_screamingsnake_case(str)` → `SCREAMING_SNAKE_CASE`
- `inflector_to_snake_case(str)` → `snake_case`
- `inflector_to_kebab_case(str)` → `kebab-case`
- `inflector_to_train_case(str)` → `Train-Case`
- `inflector_to_sentence_case(str)` → `Sentence case`
- `inflector_to_title_case(str)` → `Title Case`
- `inflector_to_table_case(str)` → `table_names`
- `inflector_to_upper_case(str)` → `UPPERCASE`
- `inflector_to_lower_case(str)` → `lowercase`

You can also use the function `inflect(case_name, value)`,

**Examples:**
```sql
SELECT inflector_to_camel_case('hello_world') as v;
┌────────────┐
│     v      │
│  varchar   │
├────────────┤
│ helloWorld │
└────────────┘

SELECT inflect('camel', 'hello_world') as v;
┌────────────┐
│     v      │
│  varchar   │
├────────────┤
│ helloWorld │
└────────────┘

SELECT inflector_to_snake_case('HelloWorld') as v;
┌─────────────┐
│      v      │
│   varchar   │
├─────────────┤
│ hello_world │
└─────────────┘

SELECT inflector_to_title_case('hello_world') as v;
┌─────────────┐
│      v      │
│   varchar   │
├─────────────┤
│ Hello World │
└─────────────┘
```

### Pluralization and Singularization

- `inflector_to_plural(str)` → plural form
- `inflector_to_singular(str)` → singular form

```sql
SELECT inflector_to_plural('person') as v;
┌─────────┐
│    v    │
│ varchar │
├─────────┤
│ people  │
└─────────┘

SELECT inflector_to_singular('ducks') as v;
┌─────────┐
│    v    │
│ varchar │
├─────────┤
│ duck    │
└─────────┘
```

### Ordinalization

- `inflector_ordinalize(str)` → ordinal string (e.g., `1st`, `2nd`)
- `inflector_deordinalize(str)` → number string (e.g., `1st` → `1`)

```sql
SELECT inflector_ordinalize('5') as v;
┌─────────┐
│    v    │
│ varchar │
├─────────┤
│ 5th     │
└─────────┘

SELECT inflector_deordinalize('21st') as v;
┌─────────┐
│    v    │
│ varchar │
├─────────┤
│ 21      │
└─────────┘
```

### Foreign Key and Module Functions

- `inflector_to_foreign_key(str)` → foreign key name (e.g., `User` → `user_id`)
- `inflector_demodulize(str)` → last module/class name (e.g., `A::B::C` → `C`)
- `inflector_deconstantize(str)` → parent module path (e.g., `A::B::C` → `A::B`)

```sql
SELECT inflector_to_foreign_key('User') as v;
┌─────────┐
│    v    │
│ varchar │
├─────────┤
│ user_id │
└─────────┘

SELECT inflector_demodulize('ActiveRecord::CoreExtensions::String::Inflections') as v;
┌────────────┐
│     v      │
│  varchar   │
├────────────┤
│ Inflection │
└────────────┘

SELECT inflector_deconstantize('ActiveRecord::CoreExtensions::String::Inflections') as v;
┌─────────┐
│    v    │
│ varchar │
├─────────┤
│ String  │
└─────────┘
```

### Predicate Functions

Check if a string matches a specific case or format:

- `inflector_is_class_case(str)`
- `inflector_is_camel_case(str)`
- `inflector_is_pascal_case(str)`
- `inflector_is_screamingsnake_case(str)`
- `inflector_is_snake_case(str)`
- `inflector_is_kebab_case(str)`
- `inflector_is_train_case(str)`
- `inflector_is_sentence_case(str)`
- `inflector_is_title_case(str)`
- `inflector_is_table_case(str)`
- `inflector_is_foreign_key(str)`

Returns `true` or `false`.

**Examples:**
```sql
SELECT inflector_is_snake_case('hello_world') as v;
┌─────────┐
│    v    │
│ boolean │
├─────────┤
│ true    │
└─────────┘

SELECT inflector_is_camel_case('HelloWorld') as v;
┌─────────┐
│    v    │
│ boolean │
├─────────┤
│ false   │
└─────────┘

SELECT inflector_is_foreign_key('user_id') as v;
┌─────────┐
│    v    │
│ boolean │
├─────────┤
│ true    │
└─────────┘
```

### Struct and Table Column Inflection

The `inflect()` function is the most powerful feature, allowing you to transform all column names in a struct or table result at once.

This example inflects the keys of a `STRUCT` or table's column names to a target case style:

**Syntax:**
```sql
SELECT * FROM inflect('case_style', {'example_field': 5, 'ExampleField2': 3});

-- or inflect an entire result

SELECT * FROM inflect('case_style', (SELECT * FROM 'example.parquet'));

```

**Supported case styles:**

- `'camel'` / `'camel_case'` → camelCase
- `'class'` / `'class_case'` → ClassCase
- `'snake'` / `'snake_case'` → snake_case
- `'kebab'` / `'kebab_case'` → kebab-case
- `'train'` / `'train_case'` → Train-Case
- `'title'` / `'title_case'` → Title Case
- `'table'` / `'table_case'` → table_names (pluralized snake_case)
- `'sentence'` / `'sentence_case'` → Sentence case
- `'upper'` / `'upper_case'` → UPPERCASE
- `'lower'` / `'lower_case'` → lowercase

**Examples:**
```sql
SELECT inflect('snake', {'firstName': 1, 'lastName': 2}) as v;
┌───────────────────────────────────────────────┐
│                       v                       │
│ struct(first_name integer, last_name integer) │
├───────────────────────────────────────────────┤
│ {'first_name': 1, 'last_name': 2}             │
└───────────────────────────────────────────────┘

SELECT inflect('class', {'first_name': 1, 'last_name': 2}) as v;
┌─────────────────────────────────────────────┐
│                      v                      │
│ struct(firstname integer, lastname integer) │
├─────────────────────────────────────────────┤
│ {'FirstName': 1, 'LastName': 2}             │
└─────────────────────────────────────────────┘

-- Table returning function.
SELECT n FROM inflect('camel', (SELECT * FROM 'example.parquet')) AS n;
┌─────────────────────────────────────────────────┐
│                        n                        │
│ struct(countervalue integer, firstname varchar) │
├─────────────────────────────────────────────────┤
│ {'counterValue': 1, 'firstName': t}             │
└─────────────────────────────────────────────────┘
```

## Real-World Examples

### Normalize CSV Column Names
```sql
-- Convert PascalCase CSV headers to snake_case
SELECT * FROM inflect('snake', (select * FROM read_csv('UserData.csv')));
-- FirstName, LastName, EmailAddress → first_name, last_name, email_address
```

### Validate Naming Conventions
```sql
-- Check if all column names follow snake_case convention
SELECT
  column_name,
  inflector_is_snake_case(column_name) as is_valid
FROM information_schema.columns
WHERE table_name = 'my_table';
```

### Schema Migration Between Conventions
```sql
-- Convert a Ruby on Rails style table to JavaScript convention
CREATE TABLE users_camel AS
SELECT * FROM inflect('camel', (select * FROM users));
```

### Generate Foreign Key Names
```sql
-- Create foreign key column names from table names
SELECT
  table_name,
  inflector_to_foreign_key(table_name) as fk_column
FROM information_schema.tables;
```

### Pluralize Table Names
```sql
-- Ensure all table names are plural
SELECT
  table_name,
  inflector_to_plural(table_name) as plural_name
FROM information_schema.tables;
```

### Clean and Standardize Text Data
```sql
-- Standardize mixed-case product categories
UPDATE products
SET category = inflector_to_title_case(category);
```

### Generate Display Labels
```sql
-- Convert snake_case column names to human-readable titles
SELECT
  'first_name' as column,
  inflector_to_title_case('first_name') as label;  -- First Name
```

## Acronym Handling

By default, case conversions capitalize words normally (e.g., `html_parser` → `HtmlParser`). You can configure acronyms so they are preserved as fully uppercase in output styles where it matters (PascalCase, camelCase, Title Case, Train-Case, Sentence case).

### Configuration Functions

- `inflector_set_acronyms(csv)` — Configure acronyms from a comma-separated string. Returns the normalized sorted list.
- `inflector_get_acronyms()` — Returns the currently configured acronyms.
- `inflector_clear_acronyms()` — Clears all acronyms, restoring default behavior.

Single-character tokens are silently ignored (minimum 2 characters).

```sql
-- Configure acronyms
SELECT inflector_set_acronyms('HTML,API,URL,JSON');
-- → API,HTML,JSON,URL

-- Now conversions preserve acronyms in output
SELECT inflector_to_pascal_case('html_parser');     -- → HTMLParser
SELECT inflector_to_camel_case('parse_html');       -- → parseHTML
SELECT inflector_to_title_case('html_parser');      -- → HTML Parser
SELECT inflector_to_train_case('html_parser');      -- → HTML-Parser
SELECT inflector_to_sentence_case('html_parser');   -- → HTML parser

-- Cases that produce all-lowercase or all-uppercase output are unaffected
SELECT inflector_to_snake_case('HTMLParser');        -- → html_parser
SELECT inflector_to_kebab_case('HTMLParser');        -- → html-parser

-- Predicates respect acronym configuration
SELECT inflector_is_pascal_case('HTMLParser');       -- → true
SELECT inflector_is_pascal_case('HtmlParser');       -- → false

-- Works with struct inflection too
SELECT inflect('pascal', {'html_parser': 1});       -- → {'HTMLParser': 1}

-- Check current acronyms
SELECT inflector_get_acronyms();    -- → API,HTML,JSON,URL

-- Clear and restore default behavior
SELECT inflector_clear_acronyms();
SELECT inflector_to_pascal_case('html_parser');      -- → HtmlParser
```

### Behavior Notes

- **First word in camelCase** is always lowercase, even if it's an acronym: `html_parser` → `htmlParser`
- **Sentence case** capitalizes the first word (or uppercases if acronym), rest lowercase except acronyms
- **Snake, kebab, screaming_snake** output is unaffected since those styles don't use mixed case
- **Thread-safe**: Acronym configuration uses a read-write lock for concurrent access

## Advanced Usage

### Nested Struct Transformation
```sql
-- Inflect nested struct keys recursively
SELECT inflect('camel', {
  'user_info': {
    'first_name': 'John',
    'last_name': 'Doe'
  }
}) as result;
```

### Bulk Column Validation
```sql
-- Find all columns that don't follow snake_case
WITH column_check AS (
  SELECT
    table_name,
    column_name,
    inflector_is_snake_case(column_name) as is_snake_case
  FROM information_schema.columns
)
SELECT * FROM column_check WHERE NOT is_snake_case;
```

### Data Pipeline Transformation
```sql
SELECT * FROM inflect('snake', (select * FROM read_csv('example.csv')));
```

## Performance Considerations

- **Transformation functions** are highly optimized and work efficiently on large datasets
- **The `inflect()` function** operates on column metadata, not data, making it very fast
- **Predicate functions** can be used in WHERE clauses and are optimized for filtering

## Tips and Best Practices

1. **Use the right case for your project**: Consistent naming improves maintainability and reduces errors
2. **Validate data early**: Use predicate functions in data quality checks during ETL
3. **Automate schema migrations**: Use `inflect()` to convert column names in bulk rather than manual renaming
4. **Combine with DuckDB's JSON/struct features**: Inflect nested data structures from APIs and config files
5. **Standardize imports**: Apply inflection when reading external data (CSV, JSON, Parquet)
6. **Use table_case for database tables**: Automatically pluralizes and converts to snake_case
7. **Document your conventions**: Choose a case style and stick with it across your project

## Error Handling

The extension provides clear error messages for common issues:

```sql
-- Unknown case style
SELECT inflect('unknown', {'foo': 1});
-- Error: Unknown inflection 'unknown'. Supported: camel, class, snake, kebab, train, title, table, sentence, upper, lower

-- Invalid arguments
SELECT inflect('snake');
-- Error: inflect() requires exactly two arguments: function name and value to inflect
```

## Frequently Asked Questions

**Q: What's the difference between `class_case` and `pascal_case`?**

A: They're the same! Both produce `PascalCase` output.

**Q: Can I use `inflect()` on a table with millions of rows?**

A: Yes! The `inflect()` function only transforms column *names*, not the data itself, so it's extremely fast regardless of table size.

**Q: Does `table_case` always pluralize?**

A: Yes, `table_case` converts to snake_case and pluralizes the name (e.g., `User` → `users`).

**Q: Can I chain transformations?**

A: Yes! You can nest `inflect()` calls or pipe results through multiple transformations.

## Contributing

The Inflector extension is open source and developed by [Query.Farm](https://query.farm).

## License

[MIT License](LICENSE)
