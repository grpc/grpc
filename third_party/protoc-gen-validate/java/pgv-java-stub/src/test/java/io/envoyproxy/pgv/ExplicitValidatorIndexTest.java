package io.envoyproxy.pgv;

import org.junit.Test;

import java.util.concurrent.atomic.AtomicBoolean;

import static org.assertj.core.api.Assertions.assertThat;

@SuppressWarnings("unchecked")
public class ExplicitValidatorIndexTest {
    class Thing {}

    @Test
    public void indexFindsValidator() throws ValidationException {
        AtomicBoolean called = new AtomicBoolean();
        ValidatorImpl validatorImpl = (ValidatorImpl<Thing>) (proto, index) -> called.set(true);

        ExplicitValidatorIndex index = new ExplicitValidatorIndex();
        index.add(Thing.class, validatorImpl);

        Thing thing = new Thing();
        Validator<Thing> validator = index.validatorFor(thing);
        assertThat(validator).isNotEqualTo(ValidatorImpl.ALWAYS_VALID);
        validator.assertValid(thing);
        assertThat(called).isTrue();
    }

    @Test
    public void indexFallsBack() throws ValidationException {
        AtomicBoolean called = new AtomicBoolean();
        ValidatorIndex fallback = new ValidatorIndex() {
            @Override
            @SuppressWarnings("unchecked")
            public <T> Validator<T> validatorFor(Class clazz) {
                called.set(true);
                return Validator.ALWAYS_VALID;
            }
        };

        ExplicitValidatorIndex index = new ExplicitValidatorIndex(fallback);
        Thing thing = new Thing();
        Validator<Thing> validator = index.validatorFor(thing);
        assertThat(validator).isNotEqualTo(ValidatorImpl.ALWAYS_VALID);
        validator.assertValid(thing);
        assertThat(called).isTrue();
    }
}
