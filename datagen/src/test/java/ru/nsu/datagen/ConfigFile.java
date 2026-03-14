package ru.nsu.datagen;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * Указывает путь к YAML-конфигу для конкретного теста.
 * Если аннотация не указана, используется "config.yaml".
 *
 * Пример:
 * <pre>
 *   {@literal @}Test
 *   {@literal @}ConfigFile("other-config.yaml")
 *   void myTest() { ... }
 * </pre>
 */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.METHOD)
public @interface ConfigFile {
    String value();
}

