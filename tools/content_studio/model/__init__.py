"""Typed authoring models used by the Content Studio UI.

The modules are loaded lazily because format codecs and models refer to each
other.  Keeping package imports side-effect free also makes the format tests
usable without importing the Qt UI.
"""

from .types import ContentDefinition

__all__ = ["ContentDefinition", "ContentWorkspace", "MapDocument", "WorldProject"]


def __getattr__(name: str):
    if name == "ContentWorkspace":
        from .content_workspace import ContentWorkspace
        return ContentWorkspace
    if name == "MapDocument":
        from .map_document import MapDocument
        return MapDocument
    if name == "WorldProject":
        from .world_project import WorldProject
        return WorldProject
    raise AttributeError(name)
