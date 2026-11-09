package ru.nsu.datagen.dataGenerator.model;

import lombok.Builder;
import lombok.Getter;
import ru.nsu.datagen.importer.explainanalyze.ScanType;
import java.util.List;

@Getter
@Builder
public class AnalyzePlanInfo {
    private final String tableName;
    private final ScanType scanType;
    private final double selectivity;
    private final long   actualRows;
    private final String rawFilter;
    private final String indexCond;
    private final long   totalRows;
    private final long   ndistinct;
    private final double joinFactor;
    private final String sourceNodeType;
    private final List<PredicateObservation> predicateObservations;
}
