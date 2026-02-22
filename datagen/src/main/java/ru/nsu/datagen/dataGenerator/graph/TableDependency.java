package ru.nsu.datagen.dataGenerator.graph;

import lombok.Getter;
import ru.nsu.datagen.dataGenerator.model.TableMetadata;
import java.util.HashSet;
import java.util.Set;

public class TableDependency {
    @Getter
    private final TableMetadata table;
    private final Set<TableDependency> dependencies;

    public TableDependency(TableMetadata table) {
        this.table = table;
        this.dependencies = new HashSet<>();
    }

    public void addDependency(TableDependency dependency) {
        dependencies.add(dependency);
    }

    public Set<TableDependency> getDependencies() { return new HashSet<>(dependencies); }

    public boolean hasDependencies() {
        return !dependencies.isEmpty();
    }
}