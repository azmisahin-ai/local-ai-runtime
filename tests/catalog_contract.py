"""Small dependency-free validator for the JSON Schema subset used by the catalog contract."""


def _schema_type_matches(value, expected):
    expected_types = expected if isinstance(expected, list) else [expected]
    for kind in expected_types:
        if kind == "object" and isinstance(value, dict):
            return True
        if kind == "array" and isinstance(value, list):
            return True
        if kind == "string" and isinstance(value, str):
            return True
        if kind == "boolean" and isinstance(value, bool):
            return True
        if kind == "integer" and isinstance(value, int) and not isinstance(value, bool):
            return True
        if kind == "number" and isinstance(value, (int, float)) and not isinstance(value, bool):
            return True
        if kind == "null" and value is None:
            return True
    return False


def validate_catalog_schema(instance, schema, root_schema=None, path="$", errors=None):
    """Validate the intentional schema keywords used here (type/required/refs/enums/bounds)."""
    if errors is None:
        errors = []
    if root_schema is None:
        root_schema = schema
    if "$ref" in schema:
        reference = schema["$ref"]
        if not reference.startswith("#/"):
            errors.append(f"{path}: only local schema references are supported, got {reference}")
            return errors
        target = root_schema
        for token in reference[2:].split("/"):
            target = target.get(token, {}) if isinstance(target, dict) else {}
        if not target:
            errors.append(f"{path}: unresolved schema reference {reference}")
            return errors
        return validate_catalog_schema(instance, target, root_schema, path, errors)

    expected_type = schema.get("type")
    if expected_type and not _schema_type_matches(instance, expected_type):
        errors.append(f"{path}: expected type {expected_type}, got {type(instance).__name__}")
        return errors
    if "const" in schema and instance != schema["const"]:
        errors.append(f"{path}: expected constant {schema['const']!r}")
    if "enum" in schema and instance not in schema["enum"]:
        errors.append(f"{path}: value {instance!r} is not in {schema['enum']!r}")
    if isinstance(instance, str):
        if "minLength" in schema and len(instance) < schema["minLength"]:
            errors.append(f"{path}: string is shorter than {schema['minLength']}")
        if "pattern" in schema:
            import re
            if re.search(schema["pattern"], instance) is None:
                errors.append(f"{path}: string does not match pattern {schema['pattern']!r}")
    if isinstance(instance, (int, float)) and not isinstance(instance, bool):
        if "minimum" in schema and instance < schema["minimum"]:
            errors.append(f"{path}: value is below minimum {schema['minimum']}")
    if isinstance(instance, dict):
        for required in schema.get("required", []):
            if required not in instance:
                errors.append(f"{path}: required property {required!r} is missing")
        for key, child_schema in schema.get("properties", {}).items():
            if key in instance:
                validate_catalog_schema(instance[key], child_schema, root_schema, f"{path}.{key}", errors)
    if isinstance(instance, list):
        if "minItems" in schema and len(instance) < schema["minItems"]:
            errors.append(f"{path}: array has fewer than {schema['minItems']} items")
        item_schema = schema.get("items")
        if item_schema:
            for index, item in enumerate(instance):
                validate_catalog_schema(item, item_schema, root_schema, f"{path}[{index}]", errors)
    return errors
