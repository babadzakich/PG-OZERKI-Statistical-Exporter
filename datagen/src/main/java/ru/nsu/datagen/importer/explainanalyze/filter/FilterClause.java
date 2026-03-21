package ru.nsu.datagen.importer.explainanalyze.filter;

import lombok.Builder;
import lombok.Getter;

@Getter
@Builder
public class FilterClause {
    private final String    column;     // "status"
    private final FilterOp  op;         // EQUALS, GT, LT, ANY, IS_NULL, LIKE...
    private final Object    value;      // "Arrived", 18, List("Arrived","Departed")
    private final boolean   isLiteral;
}
