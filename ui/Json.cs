using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace MyHotkey
{
    /// <summary>
    /// Just enough JSON for our own config file, written by hand for the same
    /// reason the engine has its own: the schema is small, entirely ours, and
    /// not worth a dependency that would show up in what users download.
    ///
    /// The writer indents, because people do open this file.
    /// </summary>
    public static class Json
    {
        // ------------------------------------------------------------ read --

        public static object Parse(string text)
        {
            int i = 0;
            object v = ParseValue(text, ref i);
            SkipWs(text, ref i);
            if (i != text.Length) throw new FormatException("trailing content after the value");
            return v;
        }

        static void SkipWs(string s, ref int i)
        {
            while (i < s.Length && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
        }

        static object ParseValue(string s, ref int i)
        {
            SkipWs(s, ref i);
            if (i >= s.Length) throw new FormatException("unexpected end of input");

            switch (s[i])
            {
                case '{': return ParseObject(s, ref i);
                case '[': return ParseArray(s, ref i);
                case '"': return ParseString(s, ref i);
                default:
                    if (Match(s, ref i, "true"))  return true;
                    if (Match(s, ref i, "false")) return false;
                    if (Match(s, ref i, "null"))  return null;
                    return ParseNumber(s, ref i);
            }
        }

        static bool Match(string s, ref int i, string word)
        {
            if (i + word.Length > s.Length) return false;
            if (string.CompareOrdinal(s, i, word, 0, word.Length) != 0) return false;
            i += word.Length;
            return true;
        }

        static Dictionary<string, object> ParseObject(string s, ref int i)
        {
            var map = new Dictionary<string, object>(StringComparer.Ordinal);
            i++;                                     // '{'
            SkipWs(s, ref i);
            if (i < s.Length && s[i] == '}') { i++; return map; }

            while (true)
            {
                SkipWs(s, ref i);
                string key = ParseString(s, ref i);
                SkipWs(s, ref i);
                if (i >= s.Length || s[i] != ':') throw new FormatException("expected a colon");
                i++;
                map[key] = ParseValue(s, ref i);
                SkipWs(s, ref i);
                if (i < s.Length && s[i] == ',') { i++; continue; }
                if (i < s.Length && s[i] == '}') { i++; return map; }
                throw new FormatException("expected a comma or a closing brace");
            }
        }

        static List<object> ParseArray(string s, ref int i)
        {
            var list = new List<object>();
            i++;                                     // '['
            SkipWs(s, ref i);
            if (i < s.Length && s[i] == ']') { i++; return list; }

            while (true)
            {
                list.Add(ParseValue(s, ref i));
                SkipWs(s, ref i);
                if (i < s.Length && s[i] == ',') { i++; continue; }
                if (i < s.Length && s[i] == ']') { i++; return list; }
                throw new FormatException("expected a comma or a closing bracket");
            }
        }

        static string ParseString(string s, ref int i)
        {
            if (i >= s.Length || s[i] != '"') throw new FormatException("expected a string");
            i++;
            var sb = new StringBuilder();
            while (i < s.Length && s[i] != '"')
            {
                if (s[i] != '\\') { sb.Append(s[i++]); continue; }
                i++;
                if (i >= s.Length) break;
                char c = s[i++];
                switch (c)
                {
                    case '"':  sb.Append('"');  break;
                    case '\\': sb.Append('\\'); break;
                    case '/':  sb.Append('/');  break;
                    case 'b':  sb.Append('\b'); break;
                    case 'f':  sb.Append('\f'); break;
                    case 'n':  sb.Append('\n'); break;
                    case 'r':  sb.Append('\r'); break;
                    case 't':  sb.Append('\t'); break;
                    case 'u':
                        if (i + 4 > s.Length) throw new FormatException("truncated \\u escape");
                        sb.Append((char)int.Parse(s.Substring(i, 4), NumberStyles.HexNumber,
                                                  CultureInfo.InvariantCulture));
                        i += 4;
                        break;
                    default: throw new FormatException("bad escape sequence");
                }
            }
            if (i >= s.Length) throw new FormatException("unterminated string");
            i++;                                     // closing quote
            return sb.ToString();
        }

        static double ParseNumber(string s, ref int i)
        {
            int start = i;
            while (i < s.Length && (char.IsDigit(s[i]) || s[i] == '-' || s[i] == '+' ||
                                    s[i] == '.' || s[i] == 'e' || s[i] == 'E')) i++;
            if (i == start) throw new FormatException("unexpected token");
            return double.Parse(s.Substring(start, i - start), CultureInfo.InvariantCulture);
        }

        // ----------------------------------------------------------- write --

        public static string Write(object value)
        {
            var sb = new StringBuilder();
            WriteValue(sb, value, 0);
            sb.Append('\n');
            return sb.ToString();
        }

        static void Indent(StringBuilder sb, int depth) { sb.Append(' ', depth * 2); }

        static void WriteValue(StringBuilder sb, object v, int depth)
        {
            if (v == null) { sb.Append("null"); return; }

            var map = v as Dictionary<string, object>;
            if (map != null) { WriteObject(sb, map, depth); return; }

            var list = v as List<object>;
            if (list != null) { WriteArray(sb, list, depth); return; }

            if (v is bool) { sb.Append(((bool)v) ? "true" : "false"); return; }

            if (v is int || v is double || v is long)
            {
                sb.Append(Convert.ToString(v, CultureInfo.InvariantCulture));
                return;
            }
            WriteString(sb, v.ToString());
        }

        static void WriteObject(StringBuilder sb, Dictionary<string, object> map, int depth)
        {
            if (map.Count == 0) { sb.Append("{}"); return; }
            sb.Append("{\n");
            int n = 0;
            foreach (var kv in map)
            {
                Indent(sb, depth + 1);
                WriteString(sb, kv.Key);
                sb.Append(": ");
                WriteValue(sb, kv.Value, depth + 1);
                if (++n < map.Count) sb.Append(',');
                sb.Append('\n');
            }
            Indent(sb, depth);
            sb.Append('}');
        }

        static void WriteArray(StringBuilder sb, List<object> list, int depth)
        {
            if (list.Count == 0) { sb.Append("[]"); return; }
            sb.Append("[\n");
            for (int i = 0; i < list.Count; i++)
            {
                Indent(sb, depth + 1);
                WriteValue(sb, list[i], depth + 1);
                if (i + 1 < list.Count) sb.Append(',');
                sb.Append('\n');
            }
            Indent(sb, depth);
            sb.Append(']');
        }

        static void WriteString(StringBuilder sb, string s)
        {
            sb.Append('"');
            foreach (char c in s)
            {
                switch (c)
                {
                    case '"':  sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n");  break;
                    case '\r': sb.Append("\\r");  break;
                    case '\t': sb.Append("\\t");  break;
                    default:
                        if (c < 0x20) sb.Append("\\u").Append(((int)c).ToString("x4"));
                        else sb.Append(c);
                        break;
                }
            }
            sb.Append('"');
        }

        // ---------------------------------------------------------- helpers --

        public static string Str(Dictionary<string, object> map, string key, string fallback)
        {
            object v;
            return (map != null && map.TryGetValue(key, out v) && v is string) ? (string)v : fallback;
        }

        public static bool Bool(Dictionary<string, object> map, string key, bool fallback)
        {
            object v;
            if (map == null || !map.TryGetValue(key, out v)) return fallback;
            if (v is bool) return (bool)v;
            if (v is double) return (double)v != 0;
            return fallback;
        }

        public static Dictionary<string, object> Obj(Dictionary<string, object> map, string key)
        {
            object v;
            if (map != null && map.TryGetValue(key, out v)) return v as Dictionary<string, object>;
            return null;
        }

        public static List<object> Arr(Dictionary<string, object> map, string key)
        {
            object v;
            if (map != null && map.TryGetValue(key, out v)) return v as List<object>;
            return null;
        }
    }
}
