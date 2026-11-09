package ru.nsu.datagen.importer.explainanalyze.filter;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class FilterParserTest {
    @Test void parsesConjunctionAndKeepsItsAtoms() {
        FilterExpression e = FilterParser.parse("(f.status = 'Arrived'::text) AND (f.delay > 10)").orElseThrow();
        assertEquals(FilterExpression.LogicOp.AND, e.getLogicOp());
        assertEquals("status", e.getChildren().get(0).getClauses().get(0).getColumn());
        assertEquals("Arrived", e.getChildren().get(0).getClauses().get(0).getValue());
        assertEquals(10L, e.getChildren().get(1).getClauses().get(0).getValue());
    }

    @Test void parsesOperatorPrecedenceAndQuotedText() {
        FilterExpression e = FilterParser.parse("(a = 'AND'::text) OR (b = 2 AND c = 3)").orElseThrow();
        assertEquals(FilterExpression.LogicOp.OR, e.getLogicOp());
        assertEquals(FilterExpression.LogicOp.AND, e.getChildren().get(1).getLogicOp());
    }

    @Test void parsesPostgresFilterFromDemoAnalyzePlan() {
        FilterExpression e = FilterParser.parse("((scheduled_departure >= '2017-08-01 07:00:00+07'::timestamp with time zone) AND (status = 'Arrived'::text))").orElseThrow();
        assertEquals(FilterExpression.LogicOp.AND, e.getLogicOp());
        assertEquals("scheduled_departure", e.getChildren().get(0).getClauses().get(0).getColumn());
        assertEquals("status", e.getChildren().get(1).getClauses().get(0).getColumn());
    }
}
