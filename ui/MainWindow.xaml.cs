using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;

namespace MyHotkey
{
    public partial class MainWindow : Window
    {
        readonly ObservableCollection<Rule> _rules = new ObservableCollection<Rule>();
        readonly Engine _engine = new Engine();
        readonly DispatcherTimer _saveTimer;

        bool _loading;       // wiring up at startup
        bool _loadingRule;   // populating the editor from a rule

        // Which field the recorder is currently filling, if any.
        enum Target { None, Trigger, ActionKeys }
        Target _recording = Target.None;
        DispatcherTimer _settle;
        string _captured = "";

        /// <summary>Names that cannot finish a shortcut on their own.</summary>
        static readonly HashSet<string> ModifierOnly = new HashSet<string>(
            new[] { "Ctrl", "Alt", "Shift", "Win",
                    "LCtrl", "RCtrl", "LAlt", "RAlt", "LShift", "RShift", "LWin", "RWin" },
            StringComparer.OrdinalIgnoreCase);

        Rule Current { get { return RuleList.SelectedItem as Rule; } }

        public MainWindow()
        {
            InitializeComponent();

            // Edits land in config.json half a second after the last change.
            // Debounced so typing a name is one write, not one per letter.
            _saveTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
            _saveTimer.Tick += (s, e) => SaveNow();

            RuleList.ItemsSource = _rules;
            LoadRules();
            ConnectToEngine();

            _engine.KeyCaptured += OnKeyCaptured;
            Closed += (s, e) => _engine.Dispose();

            if (_rules.Count > 0) RuleList.SelectedIndex = 0;
            UpdateEditorVisibility();
            UpdateHint();
        }

        // ------------------------------------------------------------ data --

        void LoadRules()
        {
            _rules.Clear();
            try
            {
                foreach (var r in Config.Load()) { Wire(r); _rules.Add(r); }
            }
            catch (Exception ex)
            {
                MessageBox.Show(this,
                    "Файл настроек не удалось прочитать:\n\n" + ex.Message + "\n\n" + Config.Path,
                    "MyHotkey", MessageBoxButton.OK, MessageBoxImage.Warning);
            }
        }

        void Wire(Rule r)
        {
            // The only edit that happens outside the editor pane is the
            // checkbox in the list; everything else goes through Editor_Changed.
            r.PropertyChanged += (s, e) =>
            {
                if (e.PropertyName == "Enabled") ScheduleSave();
            };
        }

        void ScheduleSave()
        {
            if (_loadingRule || _loading) return;
            _saveTimer.Stop();
            _saveTimer.Start();
        }

        void SaveNow()
        {
            _saveTimer.Stop();
            try
            {
                Config.Save(_rules.ToList());
                _engine.Reload();   // skips the engine's own debounce wait
            }
            catch (Exception ex)
            {
                Hint.Text = "Не удалось сохранить: " + ex.Message;
                Hint.Foreground = Brushes.Firebrick;
                return;
            }
            foreach (var r in _rules) r.NotifyChanged();   // refresh list captions
            UpdateHint();
        }

        /// <summary>
        /// Everything the status bar has to say: duplicate triggers (first rule
        /// wins, the rest silently never fire), a press-trigger on a bare
        /// modifier (kills the key as a modifier), an unfinished rule.
        /// </summary>
        void UpdateHint()
        {
            var problems = new List<string>();

            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var dup = new List<string>();
            foreach (var r in _rules)
            {
                if (!r.Enabled || !Config.IsComplete(r)) continue;
                string key = r.Trigger + "|" + r.App.ToLowerInvariant() + "|" +
                             r.AppIsExclude + "|" + r.OnRelease;
                if (!seen.Add(key) && !dup.Contains(r.Trigger)) dup.Add(r.Trigger);
            }
            if (dup.Count > 0)
                problems.Add("конфликт: " + string.Join(", ", dup) +
                             " — сработает только первое правило");

            foreach (var r in _rules)
            {
                if (r.Enabled && !r.OnRelease && ModifierOnly.Contains(r.Trigger))
                {
                    problems.Add(r.Trigger +
                        " срабатывает при нажатии и съест клавишу как модификатор — поставьте «при отпускании»");
                    break;
                }
            }

            if (Current != null && !Config.IsComplete(Current))
                problems.Add("выбранное правило не применяется, пока не заполнено");

            if (problems.Count > 0)
            {
                Hint.Text = string.Join("  ·  ", problems);
                Hint.Foreground = Brushes.Firebrick;
            }
            else
            {
                Hint.Text = "Изменения применяются сразу — движок перечитывает настройки сам.";
                Hint.Foreground = SystemColors.GrayTextBrush;
            }
        }

        // ---------------------------------------------------------- engine --

        void ConnectToEngine()
        {
            _loading = true;
            if (_engine.Connect())
            {
                _engine.Disconnected += () => Dispatcher.BeginInvoke(
                    new Action(() => ShowStatus(false, "движок отключился")));
                _engine.RequestStatus((enabled, count) => Dispatcher.BeginInvoke(new Action(() =>
                {
                    _loading = true;
                    MasterToggle.IsChecked = enabled;
                    _loading = false;
                    ShowStatus(true, "движок работает");
                })));
            }
            else
            {
                MasterToggle.IsChecked = true;
                MasterToggle.IsEnabled = false;
                ShowStatus(false, "движок не запущен — сочетания не работают, запись недоступна");
            }
            _loading = false;
        }

        void ShowStatus(bool ok, string text)
        {
            StatusText.Text = text;
            StatusText.Foreground = ok ? Brushes.Green : Brushes.Firebrick;
            RecordBtn.IsEnabled = ok;
            RecordActionBtn.IsEnabled = ok;
        }

        void MasterToggle_Changed(object sender, RoutedEventArgs e)
        {
            if (_loading) return;
            _engine.SetEnabled(MasterToggle.IsChecked == true);
        }

        // ------------------------------------------------------ rule editor --

        void RuleList_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            StopRecording();
            UpdateEditorVisibility();

            var r = Current;
            if (r == null) return;

            _loadingRule = true;
            TriggerText.Text = string.IsNullOrEmpty(r.Trigger)
                ? "Нажмите, чтобы записать" : r.Trigger;
            OnRelease.IsChecked = r.OnRelease;
            OnPress.IsChecked = !r.OnRelease;
            ActionKindBox.SelectedIndex = (int)r.Action.Kind;
            ActionKeysText.Text = r.Action.Keys.Count > 0
                ? string.Join(", ", r.Action.Keys) : "Нажмите, чтобы записать";
            TextBoxValue.Text = r.Action.Text;
            PathBox.Text = r.Action.Path;
            ArgsBox.Text = r.Action.Args;
            LayoutPrev.IsChecked = r.Action.LayoutBackwards;
            LayoutNext.IsChecked = !r.Action.LayoutBackwards;
            ScopeBox.SelectedIndex = string.IsNullOrEmpty(r.App) ? 0 : (r.AppIsExclude ? 2 : 1);
            AppBox.Text = r.App;
            AppBox.IsEnabled = ScopeBox.SelectedIndex != 0;
            NameBox.Text = r.Name;
            ShowActionPanel();
            _loadingRule = false;

            UpdateHint();
        }

        void UpdateEditorVisibility()
        {
            bool has = Current != null;
            Editor.Visibility = has ? Visibility.Visible : Visibility.Collapsed;
            EmptyPane.Visibility = has ? Visibility.Collapsed : Visibility.Visible;
        }

        /// <summary>One handler for every editor control: copy the whole pane
        /// back into the selected rule and schedule a save.</summary>
        void Editor_Changed(object sender, RoutedEventArgs e)
        {
            if (_loadingRule) return;
            var r = Current;
            if (r == null) return;

            r.OnRelease = OnRelease.IsChecked == true;
            r.Action.Kind = (ActionKind)Math.Max(0, ActionKindBox.SelectedIndex);
            r.Action.Text = TextBoxValue.Text;
            r.Action.Path = PathBox.Text.Trim();
            r.Action.Args = ArgsBox.Text.Trim();
            r.Action.LayoutBackwards = LayoutPrev.IsChecked == true;

            int scope = ScopeBox.SelectedIndex;
            r.App = scope <= 0 ? "" : AppBox.Text.Trim();
            r.AppIsExclude = scope == 2;
            AppBox.IsEnabled = scope > 0;

            r.Name = NameBox.Text.Trim();

            ShowActionPanel();
            ScheduleSave();
        }

        void ShowActionPanel()
        {
            if (ComboPanel == null) return;   // still loading the XAML
            var kind = (ActionKind)Math.Max(0, ActionKindBox.SelectedIndex);
            ComboPanel.Visibility  = kind == ActionKind.Combo  ? Visibility.Visible : Visibility.Collapsed;
            TextPanel.Visibility   = kind == ActionKind.Text   ? Visibility.Visible : Visibility.Collapsed;
            LaunchPanel.Visibility = kind == ActionKind.Launch ? Visibility.Visible : Visibility.Collapsed;
            LayoutPanel.Visibility = kind == ActionKind.Layout ? Visibility.Visible : Visibility.Collapsed;
            BlockPanel.Visibility  = kind == ActionKind.Block  ? Visibility.Visible : Visibility.Collapsed;
        }

        void Browse_Click(object sender, RoutedEventArgs e)
        {
            var dlg = new Microsoft.Win32.OpenFileDialog
            {
                Filter = "Программы (*.exe)|*.exe|Все файлы (*.*)|*.*",
                Title = "Выберите программу"
            };
            if (dlg.ShowDialog(this) == true)
            {
                PathBox.Text = dlg.FileName;   // TextChanged does the rest
            }
        }

        // -------------------------------------------------------- recorder --

        /// <summary>
        /// Recording goes through the engine rather than through key events on
        /// this window. A WPF control never sees Alt+Tab or Win+R - the shell
        /// takes them first. The engine's hook sits below the shell, so while
        /// capture is armed nothing else on the system sees the keys either.
        /// </summary>
        void StartRecording(Target target)
        {
            if (!_engine.Connected) return;

            _recording = target;
            _captured = "";
            _engine.StartCapture();

            if (target == Target.Trigger) TriggerText.Text = "Нажмите сочетание…";
            else ActionKeysText.Text = "Нажмите сочетание…";
            RecordHint.Text = "Идёт запись. Клавиатура перехвачена; щелчок по кнопке — отмена.";
        }

        void StopRecording()
        {
            if (_recording == Target.None) return;
            _engine.StopCapture();
            _recording = Target.None;
            if (_settle != null) { _settle.Stop(); _settle = null; }
            RecordHint.Text = "Ловятся любые сочетания, включая Alt+Tab и Win+R.";

            var r = Current;
            if (r != null)
            {
                TriggerText.Text = string.IsNullOrEmpty(r.Trigger)
                    ? "Нажмите, чтобы записать" : r.Trigger;
                ActionKeysText.Text = r.Action.Keys.Count > 0
                    ? string.Join(", ", r.Action.Keys) : "Нажмите, чтобы записать";
            }
        }

        void OnKeyCaptured(string chord)
        {
            Dispatcher.BeginInvoke(new Action(() =>
            {
                if (_recording == Target.None || string.IsNullOrEmpty(chord)) return;

                _captured = chord;
                if (_recording == Target.Trigger) TriggerText.Text = chord;
                else ActionKeysText.Text = chord;

                // A chord ending in a real key is finished. One that is only
                // modifiers might still be growing - or might be the whole
                // point, since a rule can live on RAlt alone - so wait a moment
                // and take it if nothing more arrives.
                if (!ModifierOnly.Contains(chord)) { Commit(); return; }

                if (_settle == null)
                {
                    _settle = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(900) };
                    _settle.Tick += (s, e) => Commit();
                }
                _settle.Stop();
                _settle.Start();
            }));
        }

        void Commit()
        {
            var target = _recording;
            var chord = _captured;
            StopRecording();

            var r = Current;
            if (r == null || string.IsNullOrEmpty(chord)) return;

            if (target == Target.Trigger)
            {
                r.Trigger = chord;
                TriggerText.Text = chord;
                // Firing on press would eat the key before it can act as a
                // modifier, which is almost never what someone means.
                if (ModifierOnly.Contains(chord) && !r.OnRelease)
                {
                    r.OnRelease = true;
                    _loadingRule = true;
                    OnRelease.IsChecked = true;
                    _loadingRule = false;
                }
            }
            else
            {
                r.Action.Keys = new List<string> { chord };
                ActionKeysText.Text = chord;
            }
            ScheduleSave();
            UpdateHint();
        }

        void Record_Click(object sender, RoutedEventArgs e)
        {
            if (_recording == Target.Trigger) StopRecording();
            else { StopRecording(); StartRecording(Target.Trigger); }
        }

        void RecordAction_Click(object sender, RoutedEventArgs e)
        {
            if (_recording == Target.ActionKeys) StopRecording();
            else { StopRecording(); StartRecording(Target.ActionKeys); }
        }

        // ------------------------------------------------------- list edits --

        void Add_Click(object sender, RoutedEventArgs e)
        {
            var r = new Rule();
            Wire(r);
            _rules.Add(r);
            RuleList.SelectedItem = r;
            RuleList.ScrollIntoView(r);

            // Adding a shortcut means the very next thing is pressing it, so
            // the recorder arms itself. Until it is recorded the rule is
            // incomplete and never written to the config.
            if (_engine.Connected) StartRecording(Target.Trigger);
        }

        void Duplicate_Click(object sender, RoutedEventArgs e)
        {
            var r = Current;
            if (r == null) return;
            var copy = r.Clone();
            Wire(copy);
            _rules.Insert(_rules.IndexOf(r) + 1, copy);
            RuleList.SelectedItem = copy;
            ScheduleSave();
        }

        void Delete_Click(object sender, RoutedEventArgs e)
        {
            var r = Current;
            if (r == null) return;

            string what = string.IsNullOrEmpty(r.Name) ? r.Trigger : r.Name;
            if (!string.IsNullOrEmpty(what))
            {
                if (MessageBox.Show(this, "Удалить правило «" + what + "»?", "MyHotkey",
                                    MessageBoxButton.OKCancel, MessageBoxImage.Question)
                    != MessageBoxResult.OK) return;
            }
            int at = _rules.IndexOf(r);
            _rules.Remove(r);
            if (_rules.Count > 0) RuleList.SelectedIndex = Math.Min(at, _rules.Count - 1);
            UpdateEditorVisibility();
            SaveNow();
        }
    }
}
