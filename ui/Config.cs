using System;
using System.Collections.Generic;
using System.IO;
using System.ComponentModel;
using System.Text;

namespace MyHotkey
{
    public enum ActionKind { Combo, Text, Launch, Layout, Block, Scold }

    public class RuleAction
    {
        public ActionKind Kind { get; set; }
        public List<string> Keys { get; set; }            // Combo
        public string Text { get; set; }                  // Text
        public string Path { get; set; }                  // Launch
        public string Args { get; set; }                  // Launch
        public bool LayoutBackwards { get; set; }         // Layout

        public RuleAction()
        {
            Kind = ActionKind.Combo;
            Keys = new List<string>();
            Text = Path = Args = "";
        }

        /// <summary>One line describing the action, for the rule list.</summary>
        public string Summary()
        {
            switch (Kind)
            {
                case ActionKind.Combo:
                    return Keys.Count == 0 ? "(не задано)" : string.Join(", ", Keys);
                case ActionKind.Text:
                    var one = Text.Replace("\r", "").Replace("\n", "⏎");
                    return one.Length > 40 ? "«" + one.Substring(0, 40) + "…»" : "«" + one + "»";
                case ActionKind.Launch:
                    return string.IsNullOrEmpty(Args) ? Path : Path + " " + Args;
                case ActionKind.Layout:
                    return LayoutBackwards ? "раскладка назад" : "раскладка вперёд";
                case ActionKind.Scold:
                    if (string.IsNullOrEmpty(Text)) return "ретро-алерт";
                    var msg = Text.Replace("\r", "").Replace("\n", " ");
                    return "ретро-алерт: «" +
                           (msg.Length > 30 ? msg.Substring(0, 30) + "…" : msg) + "»";
                default:
                    return "проглотить клавишу";
            }
        }
    }

    /// <summary>
    /// One remap. Properties rather than fields because WPF binds to properties
    /// only, and the enabled checkbox in the list writes straight back here.
    /// </summary>
    public class Rule : INotifyPropertyChanged
    {
        bool _enabled = true;

        public string Id { get; set; }
        public string Name { get; set; }
        public string Trigger { get; set; }
        public bool OnRelease { get; set; }
        public string App { get; set; }          // empty = everywhere
        public bool AppIsExclude { get; set; }
        public bool Scold { get; set; }          // nag when the old shortcut is used
        public RuleAction Action { get; set; }

        public bool Enabled
        {
            get { return _enabled; }
            set
            {
                if (_enabled == value) return;
                _enabled = value;
                var h = PropertyChanged;
                if (h != null) h(this, new PropertyChangedEventArgs("Enabled"));
            }
        }

        public event PropertyChangedEventHandler PropertyChanged;

        public Rule()
        {
            Id = Guid.NewGuid().ToString("N").Substring(0, 8);
            Name = Trigger = App = "";
            Scold = true;
            Action = new RuleAction();
        }

        /// <summary>Two-line caption for the rule list.</summary>
        public string Title
        {
            get
            {
                if (!string.IsNullOrEmpty(Name)) return Name;
                if (!string.IsNullOrEmpty(Trigger)) return Trigger;
                return "Новое правило";
            }
        }

        public string Subtitle
        {
            get
            {
                if (string.IsNullOrEmpty(Trigger)) return "сочетание ещё не записано";
                string s = Trigger + "  →  " + Action.Summary();
                if (!string.IsNullOrEmpty(App)) s += "  ·  " + ScopeSummary();
                return s;
            }
        }

        /// <summary>Refreshes every binding on this rule (empty name = all).</summary>
        public void NotifyChanged()
        {
            var h = PropertyChanged;
            if (h != null) h(this, new PropertyChangedEventArgs(""));
        }

        public string ScopeSummary()
        {
            if (string.IsNullOrEmpty(App)) return "везде";
            return (AppIsExclude ? "кроме " : "только ") + App;
        }

        public Rule Clone()
        {
            var r = (Rule)MemberwiseClone();
            r.Id = Guid.NewGuid().ToString("N").Substring(0, 8);
            r.Action = new RuleAction
            {
                Kind = Action.Kind,
                Keys = new List<string>(Action.Keys),
                Text = Action.Text,
                Path = Action.Path,
                Args = Action.Args,
                LayoutBackwards = Action.LayoutBackwards
            };
            return r;
        }
    }

    /// <summary>
    /// Reads and writes the same config.json the engine watches. Saving is all
    /// the "apply" there is: the engine notices the write and reloads within a
    /// fraction of a second, so there is no separate apply step to get wrong.
    /// </summary>
    public static class Config
    {
        public static string Path
        {
            get
            {
                string dir = System.IO.Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                    "MyHotkey");
                Directory.CreateDirectory(dir);
                return System.IO.Path.Combine(dir, "config.json");
            }
        }

        /// <summary>
        /// Master switch for nagging about replaced shortcuts. Lives beside the
        /// rules rather than inside them, so turning it off silences the lot
        /// without touching what each rule remembers.
        /// </summary>
        public static bool ScoldOldShortcuts = true;

        public static List<Rule> Load()
        {
            var rules = new List<Rule>();
            if (!File.Exists(Path)) return rules;

            var root = Json.Parse(File.ReadAllText(Path, Encoding.UTF8)) as Dictionary<string, object>;
            ScoldOldShortcuts = Json.Bool(root, "scoldOldShortcuts", true);
            var arr = Json.Arr(root, "rules");
            if (arr == null) return rules;

            foreach (var item in arr)
            {
                var o = item as Dictionary<string, object>;
                if (o == null) continue;

                var r = new Rule
                {
                    Id = Json.Str(o, "id", Guid.NewGuid().ToString("N").Substring(0, 8)),
                    Name = Json.Str(o, "name", ""),
                    Trigger = Json.Str(o, "trigger", ""),
                    Enabled = Json.Bool(o, "enabled", true),
                    OnRelease = Json.Str(o, "on", "press") == "release",
                    Scold = Json.Bool(o, "scold", true)
                };

                r.App = Json.Str(o, "app", "");
                if (string.IsNullOrEmpty(r.App))
                {
                    r.App = Json.Str(o, "notApp", "");
                    r.AppIsExclude = !string.IsNullOrEmpty(r.App);
                }

                var a = Json.Obj(o, "action");
                switch (Json.Str(a, "type", "combo"))
                {
                    case "text":
                        r.Action.Kind = ActionKind.Text;
                        r.Action.Text = Json.Str(a, "text", "");
                        break;
                    case "launch":
                        r.Action.Kind = ActionKind.Launch;
                        r.Action.Path = Json.Str(a, "path", "");
                        r.Action.Args = Json.Str(a, "args", "");
                        break;
                    case "layout":
                        r.Action.Kind = ActionKind.Layout;
                        r.Action.LayoutBackwards = Json.Str(a, "direction", "next") == "prev";
                        break;
                    case "block":
                    case "none":
                        r.Action.Kind = ActionKind.Block;
                        break;
                    case "scold":
                        r.Action.Kind = ActionKind.Scold;
                        r.Action.Text = Json.Str(a, "message", "");
                        break;
                    default:
                        r.Action.Kind = ActionKind.Combo;
                        var keys = Json.Arr(a, "keys");
                        if (keys != null)
                            foreach (var k in keys)
                                if (k is string) r.Action.Keys.Add((string)k);
                        break;
                }
                rules.Add(r);
            }
            return rules;
        }

        /// <summary>
        /// A rule the engine could act on. With live-apply there is always a
        /// moment when a rule exists but is half-filled - no trigger recorded
        /// yet, a combo with no keys. Writing it would make the whole config
        /// invalid and the engine would reject the file, so such rules live
        /// only in the window until they are finished.
        /// </summary>
        public static bool IsComplete(Rule r)
        {
            if (string.IsNullOrWhiteSpace(r.Trigger)) return false;
            switch (r.Action.Kind)
            {
                case ActionKind.Combo:  return r.Action.Keys.Count > 0;
                case ActionKind.Launch: return !string.IsNullOrWhiteSpace(r.Action.Path);
                default: return true;
            }
        }

        public static void Save(List<Rule> rules)
        {
            var list = new List<object>();
            foreach (var r in rules)
            {
                if (!IsComplete(r)) continue;

                var o = new Dictionary<string, object>();
                o["id"] = r.Id;
                if (!string.IsNullOrEmpty(r.Name)) o["name"] = r.Name;
                o["enabled"] = r.Enabled;
                o["trigger"] = r.Trigger;
                if (r.OnRelease) o["on"] = "release";
                if (!string.IsNullOrEmpty(r.App)) o[r.AppIsExclude ? "notApp" : "app"] = r.App;
                // Only the exception is written: absent means "nag", which is
                // what a config from before this feature should also mean.
                if (!r.Scold) o["scold"] = false;

                var a = new Dictionary<string, object>();
                switch (r.Action.Kind)
                {
                    case ActionKind.Text:
                        a["type"] = "text";
                        a["text"] = r.Action.Text;
                        break;
                    case ActionKind.Launch:
                        a["type"] = "launch";
                        a["path"] = r.Action.Path;
                        if (!string.IsNullOrEmpty(r.Action.Args)) a["args"] = r.Action.Args;
                        break;
                    case ActionKind.Layout:
                        a["type"] = "layout";
                        a["direction"] = r.Action.LayoutBackwards ? "prev" : "next";
                        break;
                    case ActionKind.Block:
                        a["type"] = "block";
                        break;
                    case ActionKind.Scold:
                        a["type"] = "scold";
                        if (!string.IsNullOrEmpty(r.Action.Text)) a["message"] = r.Action.Text;
                        break;
                    default:
                        a["type"] = "combo";
                        var keys = new List<object>();
                        foreach (var k in r.Action.Keys) keys.Add(k);
                        a["keys"] = keys;
                        break;
                }
                o["action"] = a;
                list.Add(o);
            }

            var root = new Dictionary<string, object>();
            root["version"] = 1;
            if (!ScoldOldShortcuts) root["scoldOldShortcuts"] = false;
            root["rules"] = list;

            // Write beside the target and swap, so a crash mid-write cannot
            // leave the engine reading a half-file.
            string tmp = Path + ".tmp";
            File.WriteAllText(tmp, Json.Write(root), new UTF8Encoding(false));
            if (File.Exists(Path)) File.Replace(tmp, Path, null);
            else File.Move(tmp, Path);
        }
    }
}
