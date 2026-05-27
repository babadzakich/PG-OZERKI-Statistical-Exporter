package ru.nsu.datagen.dataGenerator.generators.unique;

import java.util.List;
import java.util.Map;

import ru.nsu.datagen.dataGenerator.generators.ColumnGenerator;

/**
 * Генератор уникальных ключей (одиночных или составных).
 *
 * <p>Расширяет {@link ColumnGenerator} интерфейсом для пре-вычисления всего набора
 * уникальных значений сразу ({@link #generate(Map)}) и точечного пересэмплирования
 * упавших строк ({@link #regenerateRows(int)}).
 */
public interface UniqueKeyGenerator extends ColumnGenerator {
    /**
     * Генерирует все уникальные значения и помещает их в {@code columnData} по имени колонки.
     *
     * @param columnData выходная карта {@code columnName -> List<значений>}
     */
    void generate(Map<String, List<Object>> columnData);

    /**
     * Генерирует {@code batchSize} уникальных значений начиная со смещения {@code offset}.
     *
     * @param columnData выходная карта {@code columnName -> List<значений>}
     * @param offset     смещение от начала общего диапазона
     * @param batchSize  число строк в батче
     */
    void generate(Map<String, List<Object>> columnData, int offset, int batchSize);

    /**
     * Генерирует одно уникальное значение (для одиночной колонки).
     *
     * @return сгенерированное значение
     */
    List<Object> generate();

    /**
     * Генерирует count уникальных строк без продвижения stateData.
     * Используется при точечном повторе упавших строк.
     * Возвращает пустой список если генератор не поддерживает пересэмплирование.
     */
    default List<List<Object>> regenerateRows(int count) {
        return List.of();
    }
}
