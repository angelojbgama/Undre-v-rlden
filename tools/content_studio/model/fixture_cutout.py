from __future__ import annotations

from dataclasses import dataclass


PREFIX = "studio.fixtureCutout.v1."


@dataclass(
    frozen=True,
    slots=True,
)
class FixtureCutoutCell:
    x: int
    y: int
    tile_reference: int | None
    solid: bool


@dataclass(
    frozen=True,
    slots=True,
)
class FixtureCutout:
    owner_id: int
    layer_index: int
    terrain_role: str
    cells: tuple[
        FixtureCutoutCell,
        ...,
    ]


def _overrides(
    data: dict[str, object],
) -> list[object]:
    values = data.setdefault(
        "placementOverrides",
        [],
    )

    if not isinstance(
        values,
        list,
    ):
        raise ValueError(
            "placementOverrides must be an array"
        )

    return values


def remove_fixture_cutout(
    data: dict[str, object],
    owner_id: int,
) -> None:
    values = _overrides(
        data
    )

    values[:] = [
        value
        for value in values
        if not (
            isinstance(
                value,
                dict,
            )
            and value.get(
                "instanceId"
            )
            == owner_id
            and isinstance(
                value.get(
                    "propertyId"
                ),
                str,
            )
            and str(
                value.get(
                    "propertyId",
                    "",
                )
            ).startswith(
                PREFIX
            )
        )
    ]


def _entry(
    owner_id: int,
    property_id: str,
    kind: str,
    value: object,
) -> dict[str, object]:
    return {
        "instanceId": owner_id,
        "propertyId": (
            PREFIX
            + property_id
        ),
        "value": {
            "kind": kind,
            "value": value,
        },
    }


def write_fixture_cutout(
    data: dict[str, object],
    cutout: FixtureCutout,
) -> None:
    remove_fixture_cutout(
        data,
        cutout.owner_id,
    )

    values = _overrides(
        data
    )

    values.extend((
        _entry(
            cutout.owner_id,
            "layer",
            "integer",
            cutout.layer_index,
        ),
        _entry(
            cutout.owner_id,
            "role",
            "enumeration",
            cutout.terrain_role,
        ),
        _entry(
            cutout.owner_id,
            "cellCount",
            "integer",
            len(
                cutout.cells
            ),
        ),
    ))

    for index, cell in enumerate(
        cutout.cells
    ):
        base = (
            f"cell.{index}."
        )

        values.extend((
            _entry(
                cutout.owner_id,
                base + "x",
                "integer",
                cell.x,
            ),
            _entry(
                cutout.owner_id,
                base + "y",
                "integer",
                cell.y,
            ),
            _entry(
                cutout.owner_id,
                base
                + "tileReference",
                "integer",
                (
                    -1
                    if cell.tile_reference
                    is None
                    else cell.tile_reference
                ),
            ),
            _entry(
                cutout.owner_id,
                base + "solid",
                "boolean",
                cell.solid,
            ),
        ))


def read_fixture_cutout(
    data: dict[str, object],
    owner_id: int,
) -> FixtureCutout | None:
    values = data.get(
        "placementOverrides",
        [],
    )

    if not isinstance(
        values,
        list,
    ):
        return None

    properties: dict[
        str,
        tuple[str, object],
    ] = {}

    for entry in values:
        if (
            not isinstance(
                entry,
                dict,
            )
            or entry.get(
                "instanceId"
            )
            != owner_id
        ):
            continue

        property_id = entry.get(
            "propertyId"
        )

        wrapped = entry.get(
            "value"
        )

        if (
            not isinstance(
                property_id,
                str,
            )
            or not property_id.startswith(
                PREFIX
            )
            or not isinstance(
                wrapped,
                dict,
            )
        ):
            continue

        kind = wrapped.get(
            "kind"
        )

        if (
            not isinstance(
                kind,
                str,
            )
            or "value"
            not in wrapped
        ):
            continue

        properties[
            property_id[
                len(PREFIX):
            ]
        ] = (
            kind,
            wrapped["value"],
        )

    def integer(
        name: str,
    ) -> int | None:
        found = properties.get(
            name
        )

        if (
            found is None
            or found[0]
            != "integer"
        ):
            return None

        value = found[1]

        if (
            not isinstance(
                value,
                int,
            )
            or isinstance(
                value,
                bool,
            )
        ):
            return None

        return value

    layer = integer(
        "layer"
    )

    count = integer(
        "cellCount"
    )

    role_value = properties.get(
        "role"
    )

    if (
        layer is None
        or layer < 0
        or count is None
        or count <= 0
        or role_value is None
        or role_value[0]
        != "enumeration"
        or not isinstance(
            role_value[1],
            str,
        )
        or not role_value[1]
    ):
        return None

    cells: list[
        FixtureCutoutCell
    ] = []

    for index in range(
        count
    ):
        base = (
            f"cell.{index}."
        )

        x = integer(
            base + "x"
        )

        y = integer(
            base + "y"
        )

        tile_reference = integer(
            base
            + "tileReference"
        )

        solid = properties.get(
            base + "solid"
        )

        if (
            x is None
            or y is None
            or tile_reference is None
            or solid is None
            or solid[0] != "boolean"
            or not isinstance(
                solid[1],
                bool,
            )
        ):
            return None

        cells.append(
            FixtureCutoutCell(
                x=x,
                y=y,
                tile_reference=(
                    None
                    if tile_reference < 0
                    else tile_reference
                ),
                solid=solid[1],
            )
        )

    return FixtureCutout(
        owner_id=owner_id,
        layer_index=layer,
        terrain_role=role_value[1],
        cells=tuple(
            cells
        ),
    )
