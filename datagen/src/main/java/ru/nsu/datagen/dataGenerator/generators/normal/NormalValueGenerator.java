package ru.nsu.datagen.dataGenerator.generators.normal;

import java.util.List;

import ru.nsu.datagen.dataGenerator.generators.ColumnGenerator;

public interface NormalValueGenerator extends  ColumnGenerator {
    List<Object> generateValues();
}