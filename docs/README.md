# DuckDB Inflector Extension by [Query.Farm](https://query.farm)

The **Inflector** extension, developed by **[Query.Farm](https://query.farm)**, brings string case transformation and inflection capabilities directly to your SQL queries in DuckDB. Effortlessly convert, check, and manipulate string case styles, pluralization, and more—right inside your database.

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

**Examples:**
```sql
SELECT inflector_to_camel_case('hello_world') as v;
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

### Struct and Table Column Inflection

Inflect the keys of a `STRUCT` or table;s column names to a target case style:

- `inflect('case', struct_or_table)`

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
SELECT n FROM inflect('camel', (SELECT 1 AS counterValue, 't' AS first_name)) AS n;
┌─────────────────────────────────────────────────┐
│                        n                        │
│ struct(countervalue integer, firstname varchar) │
├─────────────────────────────────────────────────┤
│ {'counterValue': 1, 'firstName': t}             │
└─────────────────────────────────────────────────┘
```

## Real-World Examples

### Normalize Table Columns
```sql
SELECT * FROM inflect('snake', read_csv('example.csv'));
```

## Tips and Best Practices

1. **Use the right case for your project**: Consistent naming improves maintainability
2. **Validate data**: Use predicate functions to enforce naming conventions
3. **Automate schema migrations**: Use `inflect()` to convert column names in bulk
4. **Combine with DuckDB's JSON/struct features**: Inflect nested data structures
5. **Test with sample data**: Use the provided test cases as a reference

## Contributing

The Inflector extension is open source and developed by [Query.Farm](https://query.farm).

## License

[MIT License](LICENSE)