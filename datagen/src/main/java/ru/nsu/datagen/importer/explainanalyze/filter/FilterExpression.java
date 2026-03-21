package ru.nsu.datagen.importer.explainanalyze.filter;

import lombok.Builder;
import lombok.Getter;

import java.util.List;

@Getter
@Builder
public class FilterExpression {
    private final List<FilterClause>      clauses;    // простые предикаты
    private final List<FilterExpression> children;   // вложенные AND/OR
    private final LogicOp                 logicOp;    // AND / OR

    public enum LogicOp { AND, OR, NOT }
}
