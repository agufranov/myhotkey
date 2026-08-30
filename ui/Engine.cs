using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Threading;

namespace MyHotkey
{
    /// <summary>
    /// Client for the engine's control pipe.
    ///
    /// The one thing that genuinely needs it is recording a shortcut. A text box
    /// cannot capture Alt+Tab or Win+R, because the shell takes them long before
    /// any application sees a key event. The engine's hook sits below all of
    /// that, so while capture is armed it swallows everything and reports what
    /// was pressed here.
    /// </summary>
    public class Engine : IDisposable
    {
        NamedPipeClientStream _pipe;
        StreamWriter _writer;
        Thread _reader;
        volatile bool _stop;
        readonly Queue<Action<string>> _pending = new Queue<Action<string>>();

        /// <summary>Raised on the reader thread with a chord like "Alt+Tab".</summary>
        public event Action<string> KeyCaptured;
        public event Action Disconnected;

        public bool Connected { get { return _pipe != null && _pipe.IsConnected; } }

        public bool Connect(int timeoutMs = 1500)
        {
            try
            {
                _pipe = new NamedPipeClientStream(".", "myhotkey", PipeDirection.InOut,
                                                  PipeOptions.Asynchronous);
                _pipe.Connect(timeoutMs);
                _writer = new StreamWriter(_pipe, new UTF8Encoding(false)) { AutoFlush = true };

                _stop = false;
                _reader = new Thread(ReadLoop) { IsBackground = true, Name = "myhotkey-pipe" };
                _reader.Start();
                return true;
            }
            catch
            {
                _pipe = null;
                return false;
            }
        }

        void ReadLoop()
        {
            try
            {
                var reader = new StreamReader(_pipe, new UTF8Encoding(false));
                string line;
                while (!_stop && (line = reader.ReadLine()) != null)
                {
                    if (line.StartsWith("key ", StringComparison.Ordinal))
                    {
                        var h = KeyCaptured;
                        if (h != null) h(line.Substring(4).Trim());
                        continue;
                    }
                    if (line.StartsWith("capture timeout", StringComparison.Ordinal))
                        continue;   // the engine disarmed itself; nothing to do here

                    Action<string> waiter = null;
                    lock (_pending) if (_pending.Count > 0) waiter = _pending.Dequeue();
                    if (waiter != null) waiter(line);
                }
            }
            catch { /* the pipe went away; reported below */ }

            if (!_stop)
            {
                var h = Disconnected;
                if (h != null) h();
            }
        }

        void Send(string command, Action<string> onReply = null)
        {
            if (!Connected) return;
            try
            {
                if (onReply != null) lock (_pending) _pending.Enqueue(onReply);
                _writer.Write(command + "\n");
            }
            catch
            {
                var h = Disconnected;
                if (h != null) h();
            }
        }

        public void Reload()            { Send("reload"); }
        public void SetEnabled(bool on) { Send("enable " + (on ? "1" : "0")); }
        public void StartCapture()      { Send("capture on"); }
        public void StopCapture()       { Send("capture off"); }

        /// <summary>Asks for "status enabled=1 rules=2 capture=0".</summary>
        public void RequestStatus(Action<bool, int> onStatus)
        {
            Send("status", line =>
            {
                bool enabled = line.Contains("enabled=1");
                int rules = 0;
                int at = line.IndexOf("rules=", StringComparison.Ordinal);
                if (at >= 0)
                {
                    int end = at + 6;
                    while (end < line.Length && char.IsDigit(line[end])) end++;
                    int.TryParse(line.Substring(at + 6, end - at - 6), out rules);
                }
                onStatus(enabled, rules);
            });
        }

        public void Dispose()
        {
            _stop = true;
            try { if (Connected) StopCapture(); } catch { }
            try { if (_writer != null) _writer.Dispose(); } catch { }
            try { if (_pipe != null) _pipe.Dispose(); } catch { }
            _pipe = null;
        }
    }
}
