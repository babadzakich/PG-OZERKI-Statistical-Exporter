package ru.nsu.datagen.dataGenerator.model;

import java.util.*;

import lombok.Builder;
import lombok.Getter;

@Getter
public class ColumnMetadata {
    private final String name;
    private final String dataType;
    private final String sourceDataType;
    private final boolean isPrimaryKey;
    private final boolean isForeignKey;
    private final boolean isUnique;
    private final double nullPercentage;
    private final int recordCount;
    private final Integer maxLength;
    private final ForeignKeyMetadata foreignKeyMetadata;
    private final Map<Object, Double> mcv;
    private final int avgTupleSize;
    private final double ndistinct;
    private final boolean isArray;
    private final List<Object> histogramm;
    public List<Set<String>> uniquePeers;

    @Builder
    public ColumnMetadata(String name, String dataType, String sourceDataType, boolean isPrimaryKey,
                          boolean isForeignKey, boolean isUnique, double nullPercentage,
                          int recordCount, Integer maxLength, ForeignKeyMetadata foreignKeyMetadata,
                          Map<Object, Double> mcv, int avgTupleSize, double ndistinct, boolean isArray,
                          List<Object> histogramm, List<Set<String>> uniquePeers
    ) {
        this.name = name;
        this.isPrimaryKey = isPrimaryKey;
        this.sourceDataType = dataType;
        this.isForeignKey = isForeignKey;
        this.isUnique = isUnique;
        this.nullPercentage = nullPercentage;
        this.recordCount = recordCount;
        this.maxLength = maxLength;
        this.foreignKeyMetadata = foreignKeyMetadata;
        this.isArray = dataType.contains("[") && !dataType.contains("char");
        this.mcv = mcv;
        this.avgTupleSize = avgTupleSize;
        this.ndistinct = ndistinct;

        if (isArray) {
            char[] dataTypeCharArray = dataType.toCharArray();
            StringBuilder dataTypeBuilder = new StringBuilder();
            for (char c : dataTypeCharArray) {
                if (c == '[') {
                    break;
                }
                dataTypeBuilder.append(c);
            }
            this.dataType = dataTypeBuilder.toString();
        } else {
            this.dataType = dataType;
        }
        this.histogramm = histogramm;
        this.uniquePeers = uniquePeers;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (o == null || getClass() != o.getClass()) return false;
        ColumnMetadata that = (ColumnMetadata) o;
        return Objects.equals(name, that.name);
    }

    @Override
    public int hashCode() {
        return Objects.hash(name);
    }
}