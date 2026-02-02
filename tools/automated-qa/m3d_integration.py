"""
DarkAges Integration for Multimodal-3D Pipeline.

This module provides DarkAges-specific configuration and utilities
for the multimodal-3d-pipeline package.

Usage:
    from m3d_integration import get_darkages_pipeline, DarkAgesConfig

    async with get_darkages_pipeline() as pipeline:
        result = await pipeline.image_to_3d("reference.png")

Example with custom output:
    config = DarkAgesConfig(
        asset_subdir="characters/enemies",
    )
    async with get_darkages_pipeline(config) as pipeline:
        result = await pipeline.image_to_3d("enemy_reference.png")
"""

from __future__ import annotations

import sys
from contextlib import asynccontextmanager
from dataclasses import dataclass, field
from pathlib import Path
from typing import AsyncIterator, Optional

# Add multimodal-3d-pipeline to path if not installed
M3D_PATH = Path("C:/MCP-Servers/multimodal-3d-pipeline/src")
if M3D_PATH.exists() and str(M3D_PATH) not in sys.path:
    sys.path.insert(0, str(M3D_PATH))

from multimodal3d import Pipeline, ProjectConfig


# =============================================================================
# DarkAges Constants
# =============================================================================

DARKAGES_ROOT = Path("C:/Dev/DarkAges")
DARKAGES_CLIENT = DARKAGES_ROOT / "src" / "client"
DARKAGES_ASSETS = DARKAGES_CLIENT / "assets"
DARKAGES_GENERATED = DARKAGES_ASSETS / "generated"


# =============================================================================
# DarkAges Configuration
# =============================================================================


@dataclass
class DarkAgesConfig:
    """
    DarkAges-specific configuration for the 3D pipeline.

    This extends the base ProjectConfig with DarkAges conventions
    and default paths.

    Attributes:
        asset_subdir: Subdirectory under assets/generated for output
        godot_target_prefix: Prefix for Godot resource paths
        auto_import: Whether to automatically import to Godot
        generate_collision: Whether to generate collision shapes
        optimize_for_mobile: Whether to optimize meshes for mobile
    """

    asset_subdir: str = ""
    godot_target_prefix: str = "res://assets/generated"
    auto_import: bool = True
    generate_collision: bool = True
    optimize_for_mobile: bool = False

    # Override base paths if needed
    project_root: Optional[Path] = None
    godot_project: Optional[Path] = None
    asset_output: Optional[Path] = None

    def to_project_config(self) -> ProjectConfig:
        """Convert to base ProjectConfig."""
        project_root = self.project_root or DARKAGES_ROOT
        godot_project = self.godot_project or DARKAGES_CLIENT

        # Calculate asset output path
        if self.asset_output:
            asset_output = self.asset_output
        elif self.asset_subdir:
            asset_output = DARKAGES_GENERATED / self.asset_subdir
        else:
            asset_output = DARKAGES_GENERATED

        return ProjectConfig(
            project_root=project_root,
            godot_project=godot_project,
            asset_output=asset_output,
        )


# =============================================================================
# DarkAges Pipeline Factory
# =============================================================================


@asynccontextmanager
async def get_darkages_pipeline(
    config: Optional[DarkAgesConfig] = None,
) -> AsyncIterator[Pipeline]:
    """
    Get a configured Pipeline instance for DarkAges.

    This is the main entry point for using the multimodal-3d-pipeline
    within the DarkAges project.

    Args:
        config: Optional DarkAges-specific configuration

    Yields:
        Connected Pipeline instance

    Example:
        async with get_darkages_pipeline() as pipeline:
            result = await pipeline.image_to_3d("reference.png")
    """
    config = config or DarkAgesConfig()
    project_config = config.to_project_config()

    # Ensure output directory exists
    project_config.asset_output.mkdir(parents=True, exist_ok=True)

    pipeline = Pipeline(project_config)
    try:
        await pipeline.connect()
        yield pipeline
    finally:
        await pipeline.disconnect()


def get_default_config() -> ProjectConfig:
    """
    Get the default ProjectConfig for DarkAges.

    Use this when you need the config but want to manage
    the pipeline lifecycle yourself.

    Returns:
        ProjectConfig configured for DarkAges
    """
    return DarkAgesConfig().to_project_config()


# =============================================================================
# DarkAges-Specific Utilities
# =============================================================================


def get_asset_categories() -> list[str]:
    """
    Get list of asset categories used in DarkAges.

    Returns:
        List of category names
    """
    return [
        "characters/player",
        "characters/enemies",
        "characters/npcs",
        "environment/terrain",
        "environment/props",
        "environment/buildings",
        "weapons",
        "armor",
        "items",
        "effects",
    ]


def get_output_path_for_category(category: str) -> Path:
    """
    Get the output path for a specific asset category.

    Args:
        category: Asset category (e.g., "characters/enemies")

    Returns:
        Full path to output directory
    """
    return DARKAGES_GENERATED / category


def get_godot_resource_path(local_path: Path) -> str:
    """
    Convert a local filesystem path to a Godot resource path.

    Args:
        local_path: Local filesystem path

    Returns:
        Godot resource path (res://...)
    """
    try:
        relative = local_path.relative_to(DARKAGES_CLIENT)
        return f"res://{relative.as_posix()}"
    except ValueError:
        # Path is not under the Godot project
        return f"res://imported/{local_path.name}"


# =============================================================================
# Quick Test / Example
# =============================================================================


async def _example_usage():
    """Example usage of the DarkAges integration."""
    # Basic usage
    async with get_darkages_pipeline() as pipeline:
        # Check connection
        health = await pipeline.health_check()
        print(f"Health: {health}")

        # List available tools
        if pipeline.godot:
            tools = await pipeline.godot.list_tools()
            print(f"Godot tools: {len(tools)}")

    # With custom category
    config = DarkAgesConfig(
        asset_subdir="characters/enemies",
        generate_collision=True,
    )
    async with get_darkages_pipeline(config) as pipeline:
        print(f"Output path: {pipeline.config.asset_output}")


if __name__ == "__main__":
    import asyncio

    asyncio.run(_example_usage())
