package ru.nsu.datagen.dataGenerator.model.batchmodel;

import java.util.List;

public record ValuesChances(
        List<Object> values,
        List<Double> chances
) {}
