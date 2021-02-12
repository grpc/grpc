package io.envoyproxy.pgv;

import java.util.concurrent.ConcurrentHashMap;

/**
 * {@code ExplicitValidatorIndex} is an explicit registry of {@link Validator} instances. If no validator is registered
 * for {@code type}, a fallback validator will be used (default ALWAYS_VALID).
 */
public final class ExplicitValidatorIndex implements ValidatorIndex {
    private final ConcurrentHashMap<Class, ValidatorImpl> VALIDATOR_IMPL_INDEX = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<Class, Validator> VALIDATOR_INDEX = new ConcurrentHashMap<>();
    private final ValidatorIndex fallbackIndex;

    public ExplicitValidatorIndex() {
        this(ValidatorIndex.ALWAYS_VALID);
    }

    public ExplicitValidatorIndex(ValidatorIndex fallbackIndex) {
        this.fallbackIndex = fallbackIndex;
    }

    /**
     * Adds a {@link Validator} to the set of known validators.
     * @param forType the type to validate
     * @param validator the validator to apply
     * @return this
     */
    public <T> ExplicitValidatorIndex add(Class<T> forType, ValidatorImpl<T> validator) {
        VALIDATOR_IMPL_INDEX.put(forType, validator);
        return this;
    }

    @SuppressWarnings("unchecked")
    public <T> Validator<T> validatorFor(Class clazz) {
        return VALIDATOR_INDEX.computeIfAbsent(clazz, c ->
                proto -> VALIDATOR_IMPL_INDEX.getOrDefault(c, (p, i) -> fallbackIndex.validatorFor(c))
                        .assertValid(proto, ExplicitValidatorIndex.this));
    }
}
