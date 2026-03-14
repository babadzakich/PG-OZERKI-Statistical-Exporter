package ru.nsu.datagen.plancheck.struct;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;

@JsonIgnoreProperties(ignoreUnknown = true)
public class ExplainRoot {
    @JsonProperty("Plan")
    public PlanNode plan;
}