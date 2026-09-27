// SPDX-License-Identifier: MIT
// The troubleshooting report as one text file that a player can paste into a bug report: the summary,
// then this session's part of each log, filtered to what explains a problem.
//   - BepInEx's log (one per session): the mods' own lines, every warning and error with the lines of
//     its stack trace, and which plugins BepInEx loaded or skipped;
//   - the preloader's and the native logs (they keep every session): from this session's start on,
//     found by the process ID in the line each writes at startup; of frame generation's 5 s pipeline
//     lines, the first and last few and the one after each troubleshooting step (it covers that step),
//     and of upscaling's 10 s lines the first and last few;
//   - Streamline's log and the game's own logs: errors and warnings only;
//   - the mods' configs: the values, without their descriptions.
// Repeated lines are counted instead of repeated, long sections keep their start and end, and the
// Windows user folder is replaced by %USERPROFILE% everywhere.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using BepInEx;
namespace TGGraphicsBridge
{
    // A file the report reads (Path) or generated text (Text), under a title.
    internal sealed class ReportEntry { internal string Name, Path, Text; }
    internal static class ReportText
    {
        private const int MaxLine = 1500, MaxLines = 300;
        private static readonly string[] OwnSources = { "TG Graphics Bridge", "TG Frame Generation", "TG Better Upscaling", "TGGraphicsBridge.Autoload" };
        internal static string Build(string summary, List<ReportEntry> entries)
        {
            int pid;
            using (var process = Process.GetCurrentProcess()) pid = process.Id;
            return Build(summary, entries, pid);
        }
        // For a given session (tests read the logs of a past one).
        internal static string Build(string summary, List<ReportEntry> entries, int pid)
        {
            var sb = new StringBuilder(summary.TrimEnd());
            sb.AppendLine();
            sb.AppendLine();
            sb.AppendLine("== Logs of this session (process " + pid + ") ==");
            foreach (var entry in entries)
            {
                sb.AppendLine();
                sb.AppendLine("--- " + entry.Name + " ---");
                try { sb.AppendLine(entry.Text ?? Extract(entry.Path, pid).TrimEnd()); }
                catch (Exception ex) { sb.AppendLine("Could not read it: " + ex.Message); }
            }
            return Scrub(sb.ToString());
        }
        private static string Extract(string path, int pid)
        {
            string name = Path.GetFileName(path);
            var lines = ReadLines(path, out bool truncated);
            if (name.Equals("LogOutput.log", StringComparison.OrdinalIgnoreCase)) return Join(BepInExLines(lines));
            // Every copy of the preloader writes a header, so this session starts at the first of the headers
            // with this process ID after the last one with another. Each native log has one bootstrap line per session.
            if (name.Equals("Autoload.log", StringComparison.OrdinalIgnoreCase))
                return Join(FromSession(lines, l => l.Contains("=== TGGraphicsBridge.Autoload"), l => HasPid(l, pid), 40, truncated));
            if (name.EndsWith(".Native.log", StringComparison.OrdinalIgnoreCase))
                return Join(NativeLines(FromSession(lines, l => l.Contains(" bootstrap") && l.Contains("PID="), l => HasPid(l, pid), 40, truncated)));
            if (name.EndsWith(".cfg", StringComparison.OrdinalIgnoreCase)) return Join(lines.Where(l => l.Length > 0 && l[0] != '#' && l.Trim().Length > 0));
            // Streamline's and the game's logs: only what went wrong.
            return Join(Errors(lines));
        }
        // "PID=1234" as a whole number in the line (not the start of PID=12345).
        private static bool HasPid(string line, int pid)
        {
            string key = "PID=" + pid;
            int at = line.IndexOf(key, StringComparison.Ordinal);
            return at >= 0 && (at + key.Length == line.Length || !char.IsDigit(line[at + key.Length]));
        }
        // The file's lines, read while another process may still write it; at most its last 8 MB
        // (truncated: the file is longer).
        private static List<string> ReadLines(string path, out bool truncated)
        {
            using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
            {
                long offset = Math.Max(0, stream.Length - (8 << 20));
                truncated = offset > 0;
                stream.Seek(offset, SeekOrigin.Begin);
                using (var reader = new StreamReader(stream, Encoding.UTF8))
                {
                    var text = reader.ReadToEnd();
                    var lines = text.Replace("\r\n", "\n").Split('\n').ToList();
                    if (offset > 0 && lines.Count > 0) lines.RemoveAt(0); // a partial first line
                    return lines;
                }
            }
        }
        // From this session's start on: the first of the start lines of this process that follow the last
        // start line of another (a process ID can come round again in a later session). Without one, the
        // last lines of the file, marked.
        private static List<string> FromSession(List<string> lines, Func<string, bool> start, Func<string, bool> ours, int fallback, bool truncated)
        {
            int at = -1;
            for (int i = lines.Count - 1; i >= 0; i--)
            {
                if (!start(lines[i])) continue;
                if (ours(lines[i])) at = i;
                else if (at >= 0) break;
            }
            if (at >= 0) return lines.Skip(at).ToList();
            var tail = lines.Skip(Math.Max(0, lines.Count - fallback)).ToList();
            // Only the last 8 MB of a long log are read, so a session that started before them looks the same.
            tail.Insert(0, truncated
                ? "(no start line of this session in the last 8 MB of this log: it started before them, or its native part didn't start; the last lines follow)"
                : "(nothing from this session: its native part didn't start; the last lines of earlier sessions follow)");
            return tail;
        }
        // BepInEx's log: the mods' lines, warnings and errors with their stack traces, and plugin loading.
        private static IEnumerable<string> BepInExLines(List<string> lines)
        {
            bool keepFollowing = false; int following = 0;
            foreach (var line in lines)
            {
                bool header = line.StartsWith("[", StringComparison.Ordinal);
                if (!header)
                {
                    if (keepFollowing && following++ < 30 && line.Trim().Length > 0) yield return line;
                    continue;
                }
                bool problem = line.StartsWith("[Warning", StringComparison.Ordinal) || line.StartsWith("[Error", StringComparison.Ordinal) ||
                    line.StartsWith("[Fatal", StringComparison.Ordinal);
                bool own = OwnSources.Any(s => line.IndexOf(s + "]", StringComparison.Ordinal) >= 0); // "[Info   :TG Frame Generation] ..."
                bool loading = line.IndexOf("Loading [", StringComparison.Ordinal) >= 0 || line.IndexOf("Skipping [", StringComparison.Ordinal) >= 0 ||
                    line.IndexOf("BepInEx 5.", StringComparison.Ordinal) >= 0 || line.IndexOf("BepInEx 6.", StringComparison.Ordinal) >= 0 ||
                    line.IndexOf("plugins to load", StringComparison.Ordinal) >= 0 || line.IndexOf("patcher", StringComparison.OrdinalIgnoreCase) >= 0;
                keepFollowing = problem || own; following = 0;
                if (problem || own || loading) yield return line;
            }
        }
        // A native log: periodic summary lines thinned, everything else kept.
        private static IEnumerable<string> NativeLines(List<string> lines)
        {
            int pipelines = lines.Count(l => l.Contains(" Pipeline ")), upscaling = lines.Count(l => l.Contains(" Upscaling 10s:"));
            int pipeline = 0, upscale = 0;
            bool afterStep = false;
            foreach (var line in lines)
            {
                if (line.Contains(" Pipeline "))
                {
                    ++pipeline;
                    // The first and last three, and the one after each troubleshooting step, which covers that step.
                    if (pipeline <= 3 || pipeline > pipelines - 3 || afterStep) yield return line;
                    afterStep = false;
                    continue;
                }
                if (line.Contains(" Upscaling 10s:"))
                {
                    ++upscale;
                    if (upscale <= 3 || upscale > upscaling - 3) yield return line;
                    continue;
                }
                // Held until that Pipeline line: the next step's notes and swapchain switch come first.
                if (line.Contains("Note: Troubleshooting: done")) afterStep = true;
                yield return line;
            }
        }
        private static IEnumerable<string> Errors(List<string> lines)
        {
            var kept = lines.Where(l => l.IndexOf("error", StringComparison.OrdinalIgnoreCase) >= 0 || l.IndexOf("warn", StringComparison.OrdinalIgnoreCase) >= 0 ||
                l.IndexOf("exception", StringComparison.OrdinalIgnoreCase) >= 0 || l.IndexOf("fail", StringComparison.OrdinalIgnoreCase) >= 0).ToList();
            if (kept.Count == 0) return new[] { "(no errors or warnings)" };
            return kept.Skip(Math.Max(0, kept.Count - 60));
        }
        // Repeated lines once with a count, long lines and long sections shortened in the middle. Lines that
        // start the same way (a status line whose tail changes) count as repeats after the second in a row.
        private static string Join(IEnumerable<string> source)
        {
            var lines = new List<string>();
            string previous = null; int repeats = 0, run = 0;
            void Flush()
            {
                if (repeats > 0) lines.Add("  (" + repeats + " more line" + (repeats == 1 ? "" : "s") + " like the one above)");
                repeats = 0;
            }
            foreach (var raw in source)
            {
                string line = raw.TrimEnd('\r');
                if (line.Length > MaxLine) line = line.Substring(0, MaxLine) + " ...";
                // Native lines start with a tick count; the same message at another tick is still a repeat.
                string key = StripTick(line);
                string start = key.Length > 80 ? key.Substring(0, 80) : key;
                if (previous != null && start == previous && ++run >= 2) { ++repeats; continue; }
                if (previous == null || start != previous) { Flush(); run = 0; }
                previous = start;
                lines.Add(line);
            }
            Flush();
            if (lines.Count > MaxLines)
            {
                int head = MaxLines / 3, tail = MaxLines - head;
                lines = lines.Take(head).Concat(new[] { "  (" + (lines.Count - MaxLines) + " lines left out)" }).Concat(lines.Skip(lines.Count - tail)).ToList();
            }
            return lines.Count == 0 ? "(empty)" : string.Join("\n", lines.ToArray());
        }
        private static string StripTick(string line)
        {
            int space = line.IndexOf(' ');
            return space > 0 && line.Take(space).All(char.IsDigit) ? line.Substring(space + 1) : line;
        }
        // The Windows user folder, as either kind of slash, in any case.
        internal static string Scrub(string text)
        {
            if (string.IsNullOrEmpty(text)) return text ?? "";
            string profile;
            try { profile = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile); } catch { return text; }
            if (string.IsNullOrEmpty(profile) || profile.Length < 4) return text;
            foreach (var form in new[] { profile, profile.Replace('\\', '/') })
                text = ReplaceIgnoringCase(text, form, "%USERPROFILE%");
            return text;
        }
        private static string ReplaceIgnoringCase(string text, string find, string replacement)
        {
            int at = text.IndexOf(find, StringComparison.OrdinalIgnoreCase);
            if (at < 0) return text;
            var sb = new StringBuilder(text.Length);
            int from = 0;
            while (at >= 0)
            {
                sb.Append(text, from, at - from).Append(replacement);
                from = at + find.Length;
                at = text.IndexOf(find, from, StringComparison.OrdinalIgnoreCase);
            }
            return sb.Append(text, from, text.Length - from).ToString();
        }
        // Where a file is, for the section title: its path under the game folder, or its own name.
        internal static string EntryName(string path)
        {
            string full = Path.GetFullPath(path), root = Path.GetFullPath(Paths.GameRootPath).TrimEnd('\\', '/') + Path.DirectorySeparatorChar;
            string relative = full.StartsWith(root, StringComparison.OrdinalIgnoreCase) ? full.Substring(root.Length) : Path.GetFileName(full);
            return relative.Replace('\\', '/');
        }
    }
}
