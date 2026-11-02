package ru.nsu.datagen.dataGenerator.generators.unique.uniquegenerators;

import java.util.Arrays;
import java.util.Locale;

public enum GeneratorsTypes {
    MARKOV,
    CARTESIAN,
    SIMPLE;

    /**
     * Разбирает имя генератора без учёта регистра.
     *
     * @throws IllegalArgumentException если такого генератора нет
     */
    public static GeneratorsTypes parse(String name) {
        try {
            return valueOf(name.trim().toUpperCase(Locale.ROOT));
        } catch (IllegalArgumentException e) {
            throw new IllegalArgumentException("Unknown generator '" + name + "', expected one of "
                    + Arrays.toString(values()));
        }
    }
}
