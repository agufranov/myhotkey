using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;

namespace MyHotkey
{
    public partial class RuleEditor : Window
    {
        readonly Rule _rule;
        readonly Engine _engine;

        // Which field the recorder is currently filling, if any.
        enum Target { None, Trigger, ActionKeys }
        Target _recording = Target.None;
        string _captured = "";
        DispatcherTimer _settle;

        /// <summary>Names that cannot finish a shortcut on their own.</summary>
        static readonly HashSet<string> ModifierOnly = new HashSet<string>(
            new[] { "Ctrl", "Alt", "Shift", "Win",
                    "LCtrl", "RCtrl", "LAlt", "RAlt", "LShift", "RShift", "LWin", "RWin" },
            StringComparer.OrdinalIgnoreCase);

        public RuleEditor(Rule rule, Engine engine)
        {
            InitializeComponent();
            _rule = rule;
            _engine = engine;

            _engine.KeyCaptured += OnKeyCaptured;
            Closed += (s, e) =>
            {
                _engine.KeyCaptured -= OnKeyCaptured;
                if (_recording != Target.None) _engine.StopCapture();
            };

            if (!_engine.Connected)
            {
                RecordBtn.IsEnabled = false;
                RecordActionBtn.IsEnabled = false;
                RecordHint.Text = "Движок не запущен — записать сочетание нельзя.";
            }

            Load();
        }

        // ------------------------------------------------------------ load --

        void Load()
        {
            TriggerText.Text = string.IsNullOrEmpty(_rule.Trigger)
                ? "Нажмите, чтобы записать" : _rule.Trigger;

            OnRelease.IsChecked = _rule.OnRelease;
            OnPress.IsChecked = !_rule.OnRelease;

            ActionKindBox.SelectedIndex = (int)_rule.Action.Kind;
            ActionKeysText.Text = _rule.Action.Keys.Count > 0
                ? string.Join(", ", _rule.Action.Keys) : "Нажмите, чтобы записать";
            TextBoxValue.Text = _rule.Action.Text;
            PathBox.Text = _rule.Action.Path;
            ArgsBox.Text = _rule.Action.Args;
            LayoutPrev.IsChecked = _rule.Action.LayoutBackwards;
            LayoutNext.IsChecked = !_rule.Action.LayoutBackwards;

            if (string.IsNullOrEmpty(_rule.App)) ScopeAll.IsChecked = true;
            else if (_rule.AppIsExclude) ScopeExcept.IsChecked = true;
            else ScopeOnly.IsChecked = true;
            AppBox.Text = _rule.App;

            NameBox.Text = _rule.Name;
            ShowActionPanel();
        }

        // -------------------------------------------------------- recorder --

        /// <summary>
        /// Recording goes through the engine rather than through key events on
        /// this window. A WPF text box never sees Alt+Tab or Win+R - the shell
        /// takes them first. The engine's hook sits below the shell, so while
        /// capture is armed nothing else on the system sees the keys either.
        /// </summary>
        void StartRecording(Target target)
        {
            if (!_engine.Connected) return;

            _recording = target;
            _captured = "";
            _engine.StartCapture();

            if (target == Target.Trigger)
            {
                TriggerText.Text = "Нажмите сочетание…";
                RecordBtn.Content = TriggerText;
            }
            else ActionKeysText.Text = "Нажмите сочетание…";

            RecordHint.Text = "Идёт запись. Отпустите клавиши, когда закончите.";
        }

        void StopRecording()
        {
            if (_recording == Target.None) return;
            _engine.StopCapture();
            _recording = Target.None;
            if (_settle != null) { _settle.Stop(); _settle = null; }
            RecordHint.Text = "Ловятся любые сочетания, включая Alt+Tab и Win+R.";
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
            StopRecording();
            if (string.IsNullOrEmpty(_captured)) return;

            if (target == Target.Trigger) TriggerText.Text = _captured;
            else ActionKeysText.Text = _captured;
        }

        void Record_Click(object sender, RoutedEventArgs e)
        {
            if (_recording == Target.Trigger) StopRecording();
            else StartRecording(Target.Trigger);
        }

        void RecordAction_Click(object sender, RoutedEventArgs e)
        {
            if (_recording == Target.ActionKeys) StopRecording();
            else StartRecording(Target.ActionKeys);
        }

        // ----------------------------------------------------------- panels --

        void ActionKind_Changed(object sender, SelectionChangedEventArgs e) { ShowActionPanel(); }

        void ShowActionPanel()
        {
            if (ComboPanel == null) return;   // still loading the XAML
            var kind = (ActionKind)ActionKindIndex();
            ComboPanel.Visibility  = kind == ActionKind.Combo  ? Visibility.Visible : Visibility.Collapsed;
            TextPanel.Visibility   = kind == ActionKind.Text   ? Visibility.Visible : Visibility.Collapsed;
            LaunchPanel.Visibility = kind == ActionKind.Launch ? Visibility.Visible : Visibility.Collapsed;
            LayoutPanel.Visibility = kind == ActionKind.Layout ? Visibility.Visible : Visibility.Collapsed;
            BlockPanel.Visibility  = kind == ActionKind.Block  ? Visibility.Visible : Visibility.Collapsed;
        }

        int ActionKindIndex()
        {
            return ActionKindBox.SelectedIndex < 0 ? 0 : ActionKindBox.SelectedIndex;
        }

        void Scope_Changed(object sender, RoutedEventArgs e)
        {
            if (AppBox == null) return;
            AppBox.IsEnabled = ScopeAll.IsChecked != true;
        }

        void Browse_Click(object sender, RoutedEventArgs e)
        {
            var dlg = new Microsoft.Win32.OpenFileDialog
            {
                Filter = "Программы (*.exe)|*.exe|Все файлы (*.*)|*.*",
                Title = "Выберите программу"
            };
            if (dlg.ShowDialog(this) == true) PathBox.Text = dlg.FileName;
        }

        // ------------------------------------------------------------ save --

        void Save_Click(object sender, RoutedEventArgs e)
        {
            StopRecording();

            string trigger = TriggerText.Text;
            if (string.IsNullOrEmpty(trigger) || trigger.StartsWith("Нажмите"))
            {
                MessageBox.Show(this, "Сначала запишите сочетание.", "MyHotkey",
                                MessageBoxButton.OK, MessageBoxImage.Information);
                return;
            }
            if (ModifierOnly.Contains(trigger) && OnPress.IsChecked == true)
            {
                // Firing on press would eat the key before it can act as a
                // modifier, which is almost never what someone means.
                var answer = MessageBox.Show(this,
                    "Правило на одном модификаторе, срабатывающее при нажатии, " +
                    "сделает эту клавишу непригодной как модификатор.\n\n" +
                    "Переключить на «при отпускании»?",
                    "MyHotkey", MessageBoxButton.YesNoCancel, MessageBoxImage.Question);
                if (answer == MessageBoxResult.Cancel) return;
                if (answer == MessageBoxResult.Yes) OnRelease.IsChecked = true;
            }

            var kind = (ActionKind)ActionKindIndex();
            if (kind == ActionKind.Combo)
            {
                string keys = ActionKeysText.Text;
                if (string.IsNullOrEmpty(keys) || keys.StartsWith("Нажмите"))
                {
                    MessageBox.Show(this, "Запишите, какие клавиши отправлять.", "MyHotkey",
                                    MessageBoxButton.OK, MessageBoxImage.Information);
                    return;
                }
                _rule.Action.Keys = new List<string>(keys.Split(new[] { ", " },
                                                    StringSplitOptions.RemoveEmptyEntries));
            }
            if (kind == ActionKind.Launch && PathBox.Text.Trim().Length == 0)
            {
                MessageBox.Show(this, "Укажите, что запускать.", "MyHotkey",
                                MessageBoxButton.OK, MessageBoxImage.Information);
                return;
            }

            _rule.Trigger = trigger;
            _rule.OnRelease = OnRelease.IsChecked == true;
            _rule.Action.Kind = kind;
            _rule.Action.Text = TextBoxValue.Text;
            _rule.Action.Path = PathBox.Text.Trim();
            _rule.Action.Args = ArgsBox.Text.Trim();
            _rule.Action.LayoutBackwards = LayoutPrev.IsChecked == true;

            _rule.App = ScopeAll.IsChecked == true ? "" : AppBox.Text.Trim();
            _rule.AppIsExclude = ScopeExcept.IsChecked == true;
            _rule.Name = NameBox.Text.Trim();

            DialogResult = true;
        }

        void Cancel_Click(object sender, RoutedEventArgs e)
        {
            StopRecording();
            DialogResult = false;
        }
    }
}
