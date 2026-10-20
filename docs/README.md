<p align="center">
  <a href="https://query.farm">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="https://query.farm/media-kit/logo/wordmark-dark.svg">
      <img alt="Query.Farm" src="https://query.farm/media-kit/logo/wordmark-light.svg" height="64">
    </picture>
  </a>
</p>

# DuckDB Inflector Extension by [Query.Farm](https://query.farm)

[![DuckDB](https://img.shields.io/badge/DuckDB-community_extension-fdf1e0?logo=duckdb&logoColor=fff000)](https://duckdb.org/community_extensions/extensions/inflector.html)
[![v1.5 build](https://github.com/Query-farm/inflector/actions/workflows/MainDistributionPipeline.yml/badge.svg?branch=v1.5)](https://github.com/Query-farm/inflector/actions/workflows/MainDistributionPipeline.yml?query=branch%3Av1.5)

The **Inflector** extension brings powerful string case transformation and inflection capabilities directly to your SQL queries in DuckDB. Transform column names, validate naming conventions, handle pluralization, and more—all without leaving your database environment.

## Documentation

Full documentation, including installation, usage, the function reference, and cookbook examples, is available at:

**[https://query.farm/products/extensions/inflector](https://query.farm/products/extensions/inflector)**

## Installation

```sql
INSTALL inflector FROM community;
LOAD inflector;
```
