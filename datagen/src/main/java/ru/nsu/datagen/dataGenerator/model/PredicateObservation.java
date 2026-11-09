package ru.nsu.datagen.dataGenerator.model;

import lombok.Builder;
import lombok.Getter;
import ru.nsu.datagen.importer.explainanalyze.filter.FilterClause;

@Getter
@Builder
public class PredicateObservation {
    private final FilterClause predicate;
    private final long matchingRows;
    private final long inputRows;
    private final boolean lowerBound;
}
