package ru.nsu.datagen.dataGenerator.generators.numbergenerator;

import java.util.List;

public interface ValueGenerator {
    /**
     * Генерирует одно случайное значение
     *
     * @return сгенерированное значение
    */
    Object generateValue();

    /**
     * Генерирует случайное значение в заданном диапазоне
     *
     * @param leftBorder - левая граница диапазона включительно
     * @param rightBorder - правая граница диапазона исключительно
     * @return сгенерированное значение
     */
    Object generateValue(Object leftBorder, Object rightBorder);

    /**
     * Генерирует список уникальных случайных значений
     *
     * @param count - количество значений для генерации
     * @return список сгенерированных значений
     */
    List<Object> generateValues(int count);

    /**
     * Генерирует список уникальных случайных значений в заданном диапазоне
     *
     * @param count - количество значений для генерации
     * @param leftBorder - левая граница диапазона включительно
     * @param rightBorder - правая граница диапазона исключительно
     * @return список сгенерированных значений
     */
    List<Object> generateValues(int count, Object leftBorder, Object rightBorder);


}
