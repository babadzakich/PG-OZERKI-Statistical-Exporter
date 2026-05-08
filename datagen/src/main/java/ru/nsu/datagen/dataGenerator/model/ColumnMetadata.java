package ru.nsu.datagen.dataGenerator.model;

import java.util.List;
import java.util.Map;
import java.util.Objects;

import lombok.Builder;
import lombok.Getter;
import lombok.Setter;

@Getter
public class ColumnMetadata {
    private final String name;
    private final String dataType;
    private final String sourceDataType;
    private final boolean isPrimaryKey;
    @Setter private boolean isForeignKey;
    private final boolean isUnique;
    private final double nullFrac;
    private final int recordCount;
    private final Integer maxLength;
    private final List<ForeignKeyMetadata> foreignKeyMetadata;
    private final Map<Object, Double> mcv;
    private final int avgTupleSize;
    private final int ndistinct;
    private final boolean isArray;
    private final List<Object> histogramm;
    private final List<List<String>> compositeUniquePeers;
    private final List<List<String>> compositeForeignPeers;
    private final Map<String, Map<String, List<String>>> referencingColumns;


    @Builder
    public ColumnMetadata(String name, String dataType, String sourceDataType, boolean isPrimaryKey,
                          boolean isForeignKey, boolean isUnique, double nullFrac,
                          int recordCount, Integer maxLength, List<ForeignKeyMetadata> foreignKeyMetadata,
                          Map<Object, Double> mcv, int avgTupleSize, double ndistinct, boolean isArray,
                          List<Object> histogramm, List<List<String>> compositeUniquePeers, List<List<String>> compositeForeignPeers,
                          Map<String, Map<String, List<String>>> referencingColumns
    ) {
        this.name = name;
        this.isPrimaryKey = isPrimaryKey;
        this.sourceDataType = dataType;
        this.isForeignKey = isForeignKey;
        this.isUnique = isUnique;
        this.nullFrac = nullFrac;
        this.recordCount = recordCount;
        this.maxLength = maxLength;
        this.foreignKeyMetadata = foreignKeyMetadata;
        this.isArray = dataType.contains("[") && !dataType.contains("char");
        this.mcv = mcv;
        this.avgTupleSize = avgTupleSize;
        this.ndistinct = ndistinct == -1 ? -1 : (ndistinct < 0
            ? (int)(Math.abs(ndistinct) * recordCount)
            : (int)ndistinct);

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
        this.compositeUniquePeers = compositeUniquePeers;
        this.compositeForeignPeers = compositeForeignPeers;
        this.referencingColumns = referencingColumns;
    }

    public int getNullCount() {
        return (int) Math.round(nullFrac * recordCount);
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
