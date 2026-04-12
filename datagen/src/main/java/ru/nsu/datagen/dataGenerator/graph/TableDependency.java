package ru.nsu.datagen.dataGenerator.graph;

import java.util.HashSet;
import java.util.Set;

import lombok.Getter;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;

@Getter
public class TableDependency {
    private final TableMetadata table;
    private final Set<TableDependency> dependencies;

    public TableDependency(TableMetadata table) {
        this.table = table;
        this.dependencies = new HashSet<>();
    }

    public void addDependency(TableDependency dependency) {
        dependencies.add(dependency);
    }
}