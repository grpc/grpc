from braintree.attribute_getter import AttributeGetter as AttributeGetter

class SuccessfulResult(AttributeGetter):
    @property
    def is_success(self): ...
