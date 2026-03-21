package ru.nsu.datagen.importer.explainanalyze.filter;

public enum FilterOp {
    EQUALS, NOT_EQUALS,
    GT, GTE, LT, LTE,
    ANY, NOT_ANY,
    IS_NULL, IS_NOT_NULL,
    LIKE, ILIKE,
    BETWEEN
}
