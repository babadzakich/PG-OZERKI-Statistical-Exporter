package ru.nsu.datagen.dataGenerator.model;

import java.util.List;
import java.util.Objects;
import java.util.Map;

import lombok.Builder;
import lombok.Getter;

@Builder
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

    public ColumnMetadata(String name, String dataType, String sourceDataType, boolean isPrimaryKey,
                          boolean isForeignKey, boolean isUnique, double nullPercentage,
                          int recordCount, Integer maxLength, ForeignKeyMetadata foreignKeyMetadata,
                          Map<Object, Double> mcv, int avgTupleSize, double ndistinct, boolean isArray, List<Object> histogramm) {
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
            int pos = dataType.indexOf('[');
            if (pos != -1) {
                this.dataType = dataType.substring(0, pos);
            } else {
                this.dataType = dataType;
            }
        } else {
            this.dataType = dataType;
        }
        this.histogramm = histogramm;
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