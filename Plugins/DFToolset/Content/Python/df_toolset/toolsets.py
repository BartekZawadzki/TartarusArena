"""DF Toolset — Dark Factory evidence tools exposed through Epic's Unreal MCP server (UE 5.8+).

Pattern taken from Epic's documentation of Python toolsets: a class decorated with
`@unreal.uclass()` deriving `unreal.ToolsetDefinition`, each tool a `@staticmethod` decorated with
`@toolset_registry.tool_call`; type hints drive the JSON schema and Google-style docstrings
(Args/Returns) become the tool description. Discovered at editor startup from any plugin's
Content/Python/ directory; `ModelContextProtocol.RefreshTools` re-polls after edits.

Registered by ../init_unreal.py via `_registration.register()` (Epic's EditorToolset pattern,
read from Engine/Plugins/Experimental/Toolsets/EditorToolset on UE 5.8.3). Decorator order is
`@toolset_registry.tool_call` above `@staticmethod`, as in Epic's ActorTools.
"""
import contextlib
import io
import os
import traceback

import unreal
import toolset_registry
from toolset_registry.registration import Registration


@unreal.uclass()
class DFTools(unreal.ToolsetDefinition):
    """Dark Factory evidence tools: execute Python in the editor with captured output, capture a
    viewport screenshot to a file, list the actors of the open level, and run automation tests."""

    @toolset_registry.tool_call
    @staticmethod
    def run_python(code: str) -> str:
        """Execute Python source in the editor's Python environment and return what it printed.

        Args:
            code: Python source to execute. `unreal` is already imported.

        Returns:
            Captured stdout, followed by a traceback if the code raised.
        """
        buf = io.StringIO()
        scope = {"unreal": unreal, "__name__": "__df__"}
        try:
            with contextlib.redirect_stdout(buf):
                exec(compile(code, "<df_run_python>", "exec"), scope)
        except Exception:
            buf.write("\n" + traceback.format_exc())
        return buf.getvalue()

    @toolset_registry.tool_call
    @staticmethod
    def take_screenshot(file_name: str, width: int = 1920, height: int = 1080) -> str:
        """Request a high-resolution screenshot of the active viewport.

        Args:
            file_name: File name (no directory) for the PNG, written under Saved/Screenshots.
            width: Horizontal resolution in pixels.
            height: Vertical resolution in pixels.

        Returns:
            The directory the screenshot is written to (the capture completes on a later frame).
        """
        unreal.AutomationLibrary.take_high_res_screenshot(width, height, file_name)
        return os.path.join(unreal.Paths.project_saved_dir(), "Screenshots")

    @toolset_registry.tool_call
    @staticmethod
    def list_level_actors(class_filter: str = "") -> str:
        """List the actors in the currently open level.

        Args:
            class_filter: Optional substring of the class name to keep (case-insensitive).

        Returns:
            One line per actor: label, class, location; then a COUNT line.
        """
        subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        lines = []
        for actor in subsystem.get_all_level_actors():
            cls = actor.get_class().get_name()
            if class_filter and class_filter.lower() not in cls.lower():
                continue
            loc = actor.get_actor_location()
            lines.append(f"{actor.get_actor_label()}\t{cls}\t({loc.x:.1f}, {loc.y:.1f}, {loc.z:.1f})")
        lines.append(f"COUNT {len(lines)}")
        return "\n".join(lines)

    @toolset_registry.tool_call
    @staticmethod
    def run_automation_tests(test_filter: str) -> str:
        """Start automation tests matching a filter inside the running editor.

        Args:
            test_filter: Automation test filter, e.g. "Project.Combat".

        Returns:
            The console command issued. Results land in the Output Log (LogAutomationController).
            For a machine-readable report (index.json) use the headless CLI with
            -ReportExportPath instead; that is the T1 evidence path.
        """
        cmd = f"Automation RunTests {test_filter}"
        unreal.SystemLibrary.execute_console_command(None, cmd)
        return cmd


_registration = Registration([DFTools])
