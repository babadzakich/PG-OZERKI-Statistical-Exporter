package ru.nsu.datagen.argValidation;

import com.beust.jcommander.IParameterValidator;
import com.beust.jcommander.ParameterException;

import java.io.File;

public class FilePath implements IParameterValidator {
    public void validate(String name, String value) throws ParameterException {
        if (new File(value).exists() == false) {
            throw new ParameterException("Parameter " + name + " should be correct filepath: " + value );
        }
    }
}

