package ru.nsu.datagen.plancheck.struct;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;

import java.util.*;

@JsonIgnoreProperties(ignoreUnknown = true)
public class PlanNode {
    @JsonProperty("Node Type")
    public String nodeType;

    @JsonProperty("Plans")
    public List<PlanNode> plans;

    public List<PlanNode> getPlans() {
        if (plans == null) {
            plans = new ArrayList<>();
        }
        return plans;
    }
}