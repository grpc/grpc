package io.envoyproxy.pgv;

import org.junit.Test;

import java.util.Map;
import java.util.HashMap;

import static org.assertj.core.api.Assertions.assertThatThrownBy;

public class MapValidationTest {
    @Test
    public void minWorks() throws ValidationException {
        Map<String,String> map = new HashMap<>();
        map.put("1", "ONE");
        map.put("2", "TWO");

        // Equals
        MapValidation.min("x", map, 2);
        // Not Equals
        assertThatThrownBy(() -> MapValidation.min("x", map, 3)).isInstanceOf(ValidationException.class);
    }

    @Test
    public void maxWorks() throws ValidationException {
        Map<String,String> map = new HashMap<>();
        map.put("1", "ONE");
        map.put("2", "TWO");

        // Equals
        MapValidation.max("x", map, 2);
        // Not Equals
        assertThatThrownBy(() -> MapValidation.max("x", map, 1)).isInstanceOf(ValidationException.class);
    }

    @Test
    public void noSparseWorks() throws ValidationException {
        Map<String,String> map = new HashMap<>();
        map.put("1", "ONE");
        map.put("2", null);

        // Sparse Map
        assertThatThrownBy(() -> MapValidation.noSparse("x", map)).isInstanceOf(ValidationException.class);
    }
}
