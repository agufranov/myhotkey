using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace MyHotkey
{
    public partial class MainWindow : Window
    {
        readonly ObservableCollection<Rule> _rules = new ObservableCollection<Rule>();
        readonly Engine _engine = new Engine();
        bool _loading;

        public MainWindow()
        {
            InitializeComponent();
            Grid.ItemsSource = _rules;

            LoadRules();
            ConnectToEngine();

            _rules.CollectionChanged += (s, e) => Save();
            Closed += (s, e) => _engine.Dispose();
        }

        // ------------------------------------------------------------ data --

        void LoadRules()
        {
            _rules.Clear();
            try
            {
                foreach (var r in Config.Load())
                {
                    r.PropertyChanged += (s, e) => Save();   // the Enabled checkbox
                    _rules.Add(r);
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show(this,
                    "Файл настроек не удалось прочитать:\n\n" + ex.Message +
                    "\n\n" + Config.Path,
                    "MyHotkey", MessageBoxButton.OK, MessageBoxImage.Warning);
            }
            UpdateHint();
        }

        void Save()
        {
            if (_loading) return;
            try
            {
                Config.Save(_rules.ToList());
                // The engine watches the file, so saving is the whole of
                // "apply". The nudge is only to skip the debounce wait.
                _engine.Reload();
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, "Не удалось сохранить настройки:\n\n" + ex.Message,
                                "MyHotkey", MessageBoxButton.OK, MessageBoxImage.Error);
            }
            UpdateHint();
        }

        /// <summary>
        /// Two rules with the same trigger and overlapping scope: the first one
        /// wins and the second silently never fires. Worth saying out loud.
        /// </summary>
        void UpdateHint()
        {
            var seen = new Dictionary<string, Rule>(StringComparer.OrdinalIgnoreCase);
            var clashes = new List<string>();

            foreach (var r in _rules)
            {
                if (!r.Enabled || string.IsNullOrEmpty(r.Trigger)) continue;
                string key = r.Trigger + "|" + r.App.ToLowerInvariant() + "|" + r.AppIsExclude +
                             "|" + r.OnRelease;
                if (seen.ContainsKey(key))
                {
                    if (!clashes.Contains(r.Trigger)) clashes.Add(r.Trigger);
                }
                else seen[key] = r;
            }

            if (clashes.Count > 0)
            {
                Hint.Text = "Конфликт: " + string.Join(", ", clashes) +
                            " — сработает только первое правило в списке.";
                Hint.Foreground = (Brush)FindResource("Danger");
            }
            else
            {
                Hint.Text = "Изменения применяются сразу — движок перечитывает настройки сам.";
                Hint.Foreground = (Brush)FindResource("Muted");
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
                    ShowStatus(true, "движок работает, правил: " + count);
                })));
            }
            else
            {
                MasterToggle.IsChecked = true;
                MasterToggle.IsEnabled = false;
                ShowStatus(false, "движок не запущен");
            }
            _loading = false;
        }

        void ShowStatus(bool ok, string text)
        {
            StatusText.Text = text;
            StatusText.Foreground = (Brush)FindResource(ok ? "Ok" : "Danger");
            StatusPill.Background = new SolidColorBrush(ok
                ? Color.FromRgb(0xEC, 0xFD, 0xF3)
                : Color.FromRgb(0xFE, 0xF2, 0xF2));
        }

        void MasterToggle_Changed(object sender, RoutedEventArgs e)
        {
            if (_loading) return;
            _engine.SetEnabled(MasterToggle.IsChecked == true);
        }

        // ----------------------------------------------------------- edits --

        void Add_Click(object sender, RoutedEventArgs e)
        {
            var rule = new Rule();
            if (Edit(rule, "Новое правило"))
            {
                rule.PropertyChanged += (s, ev) => Save();
                _rules.Add(rule);           // triggers Save through CollectionChanged
                Grid.SelectedItem = rule;
            }
        }

        void Edit_Click(object sender, RoutedEventArgs e) { EditSelected(); }
        void Grid_DoubleClick(object sender, RoutedEventArgs e) { EditSelected(); }

        void EditSelected()
        {
            var rule = Grid.SelectedItem as Rule;
            if (rule == null) return;

            var copy = rule.Clone();
            copy.Id = rule.Id;
            if (!Edit(copy, "Изменить правило")) return;

            int at = _rules.IndexOf(rule);
            copy.PropertyChanged += (s, ev) => Save();
            _rules[at] = copy;
            Grid.SelectedItem = copy;
            Save();
        }

        void Duplicate_Click(object sender, RoutedEventArgs e)
        {
            var rule = Grid.SelectedItem as Rule;
            if (rule == null) return;
            var copy = rule.Clone();
            copy.PropertyChanged += (s, ev) => Save();
            _rules.Insert(_rules.IndexOf(rule) + 1, copy);
            Grid.SelectedItem = copy;
        }

        void Delete_Click(object sender, RoutedEventArgs e)
        {
            var rule = Grid.SelectedItem as Rule;
            if (rule == null) return;

            string what = string.IsNullOrEmpty(rule.Name) ? rule.Trigger : rule.Name;
            if (MessageBox.Show(this, "Удалить правило «" + what + "»?", "MyHotkey",
                                MessageBoxButton.OKCancel, MessageBoxImage.Question)
                != MessageBoxResult.OK) return;
            _rules.Remove(rule);
        }

        bool Edit(Rule rule, string title)
        {
            var dlg = new RuleEditor(rule, _engine) { Owner = this, Title = title };
            bool ok = dlg.ShowDialog() == true;
            if (ok) UpdateHint();
            return ok;
        }
    }
}
