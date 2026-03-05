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

    @JsonProperty(value = "Relation Name", defaultValue = "-")
    public String relName;

    @JsonProperty(value = "Index Name", defaultValue = "-")
    public String index;

    public List<PlanNode> getPlans() {
        if (plans == null) {
            plans = new ArrayList<>();
        }
        return plans;
    }

    public String getFieldSum() {
        return nodeType + relName + index;
    }

    public String getNodeType(){
        return nodeType;
    }
}