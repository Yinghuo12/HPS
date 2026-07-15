// LoginClient.cs — HTTP /login /register(明文登录, 拿 token)
//
// 对应服务端 login 服的 HttpServer:
//   POST http://<host>:<port>/login     body {"name","password"} -> {"ok","token","account_id"}
//   POST http://<host>:<port>/register  body {"name","password"} -> {"ok","account_id"}
//
// 用 UnityWebRequest(Coroutine), 必须从 MonoBehaviour StartCoroutine 调用。
using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Networking;

namespace Ddt.Net {
public static class LoginClient {
    public struct LoginResult {
        public bool ok;
        public string token;
        public long account_id;
        public string msg;
    }

    /// <summary>登录协程。onDone 在主线程回调。</summary>
    public static IEnumerator Login(string host, int port, string name, string password,
                                    System.Action<LoginResult> onDone) {
        string url = $"http://{host}:{port}/login";
        var body = new Dictionary<string, string> { { "name", name }, { "password", password } };
        yield return PostJson(url, body, json => {
            var r = ParseLogin(json);
            onDone?.Invoke(r);
        });
    }

    /// <summary>注册协程。</summary>
    public static IEnumerator Register(string host, int port, string name, string password,
                                       System.Action<bool, long, string> onDone) {
        string url = $"http://{host}:{port}/register";
        var body = new Dictionary<string, string> { { "name", name }, { "password", password } };
        yield return PostJson(url, body, json => {
            bool ok = false; long id = 0; string msg = "";
            try {
                var j = JsonUtility.FromJson<MiniJson>(json);
                ok = j.ok;
                id = j.account_id;
                msg = j.msg;
            } catch (System.Exception e) {
                msg = "parse error: " + e.Message;
            }
            onDone?.Invoke(ok, id, msg);
        });
    }

    // ---- 极简 JSON 发送 ----
    private static IEnumerator PostJson(string url, Dictionary<string,string> body,
                                        System.Action<string> onResp) {
        // 手拼极简 JSON(字段少, 不含特殊字符即可)
        string json = "{\"name\":\"" + body["name"] + "\",\"password\":\"" + body["password"] + "\"}";
        byte[] bytes = System.Text.Encoding.UTF8.GetBytes(json);
        using (var req = new UnityWebRequest(url, "POST")) {
            req.uploadHandler = new UploadHandlerRaw(bytes);
            req.downloadHandler = new DownloadHandlerBuffer();
            req.SetRequestHeader("Content-Type", "application/json");
            req.timeout = 5;
            yield return req.SendWebRequest();
            string resp = "";
#if UNITY_2020_2_OR_NEWER
            if (req.result == UnityWebRequest.Result.Success)
#else
            if (!req.isHttpError && !req.isNetworkError)
#endif
                resp = req.downloadHandler.text;
            else
                Debug.LogWarning($"[LoginClient] {url} fail: {req.error}");
            onResp?.Invoke(resp);
        }
    }

    // ---- 使用标准的 JsonUtility 解析响应（安全、支持空格） ----
    private static LoginResult ParseLogin(string json) {
        var r = new LoginResult();
        if (string.IsNullOrEmpty(json)) { r.msg = "empty response"; return r; }
        try {
            var j = JsonUtility.FromJson<MiniJson>(json);
            r.ok = j.ok;
            r.token = j.token;
            r.account_id = j.account_id;
            r.msg = j.msg;
        } catch (System.Exception e) {
            r.msg = "parse error: " + e.Message;
        }
        return r;
    }

    // ⚠️ 必须声明为 public，否则 Unity 的 JsonUtility 无法反序列化其字段
    [System.Serializable] 
    public class MiniJson { 
        public bool ok; 
        public string token; 
        public long account_id; 
        public string msg; 
    }
}
}