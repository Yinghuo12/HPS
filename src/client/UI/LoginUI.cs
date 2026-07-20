// LoginUI.cs — 登录界面
//
// 自动搭建 UI(账号/密码输入框 + 登录/注册按钮 + 状态文本), 无需手动拼 Canvas。
// 流程: 登录按钮 → HTTP /login 拿 token → 连 gate 发 LOGIN → 收 LOGIN_RESP 成功 → 进大厅场景。
//
// 挂在 LoginScene 的一个空 GameObject "LoginUI" 上。
// Inspector 填地址; 场景名(Lobby/Battle)要和 Build Settings 里的一致。
using UnityEngine;
using UnityEngine.UI;
using UnityEngine.SceneManagement;
using Ddt.Net;
using Ddt.Net.Game;
using static Ddt.Net.Game.DebugLog;

namespace Ddt.Net.UI {
public class LoginUI : MonoBehaviour {
    [Header("登录服(HTTP)")]
    public string loginHost = "127.0.0.1";
    public int loginPort = 8200;
    [Header("网关(TCP)")]
    public string gateHost = "127.0.0.1";
    public int gatePort = 8100;
    [Header("场景名")]
    public string lobbyScene = "LobbyScene";

    private InputField serverInput_, gateInput_, nameInput_, pwdInput_;
    private Text statusText_;
    private bool connecting_ = false;

    void Start() {
        // 强制窗口模式 + 1280x720, 不全屏(覆盖 PlayerSettings, 解决打包后仍全屏)
        Screen.fullScreenMode = FullScreenMode.Windowed;
        Screen.SetResolution(1280, 720, FullScreenMode.Windowed);
        // 去掉 Skybox 3D 背景: 主相机改纯色
        var cam = Camera.main;
        if (cam != null) {
            cam.clearFlags = CameraClearFlags.SolidColor;
            cam.backgroundColor = new Color(0.15f, 0.2f, 0.3f);
        }
        BuildUI();
        // 订阅登录/注册/性别响应
        NetworkManager.Instance.Dispatcher.Subscribe(MsgId.LOGIN_RESP, OnLoginResp);
        NetworkManager.Instance.Dispatcher.Subscribe(MsgId.REGISTER_RESP, OnRegisterResp);
        NetworkManager.Instance.Dispatcher.Subscribe(MsgId.SET_GENDER_RESP, OnSetGenderResp);
        NetworkManager.Instance.Dispatcher.Subscribe(MsgId.ERROR, OnError);
    }

    private void OnLoginResp(byte[] bytes) {
        var m = LoginResp.Parser.ParseFrom(bytes);
        if (m.Result == (int)Result.Success) {
            Session.MyAccountId = m.AccountId;
            Session.MyName = m.Name;
            Session.Gender = (int)m.Gender;
            NetworkManager.Instance.MarkLoggedIn();
            // gender=0 表示首次登录未选角色 → 弹角色选择界面
            if ((int)m.Gender == 0) {
                statusText_.text = "请选择角色";
                ShowCharSelect();
            } else {
                statusText_.text = $"登录成功: {m.Name}";
                statusText_.color = Color.green;
                SceneManager.LoadScene(lobbyScene);
            }
        } else {
            statusText_.text = "网关登录失败: " + m.Msg;
            statusText_.color = Color.red;
            connecting_ = false;
            // 重连场景下 token 失效(AUTH_FAIL): 停止重连, 提示重新登录
            if (NetworkManager.Instance.ConnState == NetworkManager.ReconnectState.Reconnected
                || NetworkManager.Instance.ConnState == NetworkManager.ReconnectState.Reconnecting) {
                NetworkManager.Instance.StopReconnectAndRelogin();
                statusText_.text = "登录已过期(token 失效), 请重新登录";
            }
        }
    }

    private void OnError(byte[] bytes) {
        var m = ErrorNotify.Parser.ParseFrom(bytes);
        statusText_.text = $"错误: {m.Msg}";
        statusText_.color = Color.red;
        connecting_ = false;
    }

    private void OnRegisterResp(byte[] bytes) {
        var m = RegisterResp.Parser.ParseFrom(bytes);
        if (m.Result == (int)Result.Success) {
            statusText_.text = $"注册成功 id={m.AccountId}, 请登录";
            statusText_.color = Color.green;
        } else {
            statusText_.text = "注册失败: " + m.Msg;
            statusText_.color = Color.red;
        }
    }

    private void OnSetGenderResp(byte[] bytes) {
        var m = SetGenderResp.Parser.ParseFrom(bytes);
        if (m.Result == (int)Result.Success) {
            statusText_.text = "角色选择成功";
            statusText_.color = Color.green;
            // 销毁角色选择面板, 进大厅
            if (charSelectPanel_ != null) { Destroy(charSelectPanel_); charSelectPanel_ = null; }
            SceneManager.LoadScene(lobbyScene);
        } else {
            statusText_.text = "设置失败: " + m.Msg;
            statusText_.color = Color.red;
        }
    }

    // ---- 角色选择界面 ----
    private GameObject charSelectPanel_;

    private void ShowCharSelect() {
        if (charSelectPanel_ != null) return;
        var canvas = FindFirstObjectByType<Canvas>();
        if (canvas == null) return;

        charSelectPanel_ = new GameObject("CharSelectPanel", typeof(RectTransform));
        charSelectPanel_.transform.SetParent(canvas.transform, false);
        var prt = charSelectPanel_.GetComponent<RectTransform>();
        prt.anchorMin = Vector2.zero; prt.anchorMax = Vector2.one;
        prt.offsetMin = prt.offsetMax = Vector2.zero;

        var img = charSelectPanel_.AddComponent<Image>();
        img.color = new Color(0.1f, 0.1f, 0.15f, 0.95f);

        MakeText(charSelectPanel_.transform, "Title", new Vector2(0, 200), 30, "选择你的角色");

        // 左边: 男(面朝右 player1_r.png), 右边: 女(面朝左 player2.png)
        CreateCharPreview(charSelectPanel_.transform, new Vector2(-220, 0), "player1_r", "男", () => OnPickGender(1));
        CreateCharPreview(charSelectPanel_.transform, new Vector2(220, 0), "player2", "女", () => OnPickGender(2));

        MakeText(charSelectPanel_.transform, "Tip", new Vector2(0, -220), 16, "点击图片选择性别, 之后可在游戏中切换");
    }

    private void CreateCharPreview(Transform parent, Vector2 pos, string spriteName, string label, UnityEngine.Events.UnityAction onClick) {
        var card = new GameObject("Char_" + label, typeof(RectTransform));
        card.transform.SetParent(parent, false);
        var crt = card.GetComponent<RectTransform>();
        crt.sizeDelta = new Vector2(250, 350);
        crt.anchoredPosition = pos;

        // 贴图: 先尝试 Sprite 类型, 失败则用 Texture2D 动态转 Sprite(不依赖导入设置)
        var spriteObj = new GameObject("Sprite", typeof(RectTransform));
        spriteObj.transform.SetParent(card.transform, false);
        spriteObj.AddComponent<CanvasRenderer>();
        var img = spriteObj.AddComponent<Image>();
        img.raycastTarget = false;
        Sprite sp = Resources.Load<Sprite>("Players/" + spriteName);
        if (sp == null) {
            Texture2D tex = Resources.Load<Texture2D>("Players/" + spriteName);
            if (tex != null) {
                sp = Sprite.Create(tex, new Rect(0, 0, tex.width, tex.height), new Vector2(0.5f, 0.5f), 100f);
            }
        }
        if (sp != null) {
            img.sprite = sp;
            img.preserveAspect = true;
        } else {
            img.color = new Color(0.3f, 0.3f, 0.3f, 0.5f);   // 贴图缺失时灰色占位
        }
        var srt = img.rectTransform;
        srt.sizeDelta = new Vector2(200, 300);
        srt.anchoredPosition = new Vector2(0, 20);

        // 标签
        var lbl = MakeText(card.transform, "Label", new Vector2(0, -140), 24, label);

        // 可点击按钮层
        var btnGo = new GameObject("Btn", typeof(RectTransform));
        btnGo.transform.SetParent(card.transform, false);
        var btnImg = btnGo.AddComponent<Image>();
        btnImg.color = new Color(0, 0, 0, 0.01f);   // 透明但可点击
        var brt = btnGo.GetComponent<RectTransform>();
        brt.anchorMin = Vector2.zero; brt.anchorMax = Vector2.one;
        brt.offsetMin = brt.offsetMax = Vector2.zero;
        var btn = btnGo.AddComponent<Button>();
        btn.onClick.AddListener(onClick);
    }

    private void OnPickGender(int gender) {
        GameFacade.SendSetGender((Gender)gender);
        statusText_.text = $"正在设置: {(gender == 1 ? "男" : "女")}...";
        statusText_.color = Color.white;
    }

    // ---- UI 搭建 ----
    private void BuildUI() {
        // --- 新增下面这段代码：自动检测并创建事件系统 ---
        if (FindFirstObjectByType<UnityEngine.EventSystems.EventSystem>() == null) {
            var esGo = new GameObject("EventSystem");
            esGo.AddComponent<UnityEngine.EventSystems.EventSystem>();
            esGo.AddComponent<UnityEngine.EventSystems.StandaloneInputModule>();
            DontDestroyOnLoad(esGo); // <-- 保护它，使其跨场景不销毁
        }
        var canvas = FindFirstObjectByType<Canvas>();
        if (canvas == null) {
            var cgo = new GameObject("LoginCanvas");
            canvas = cgo.AddComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            cgo.AddComponent<GraphicRaycaster>();
        }
        var scaler = canvas.GetComponent<CanvasScaler>();
        if (scaler == null) scaler = canvas.gameObject.AddComponent<CanvasScaler>();
        scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
        scaler.referenceResolution = new Vector2(1280, 720);
        scaler.screenMatchMode = CanvasScaler.ScreenMatchMode.MatchWidthOrHeight;
        scaler.matchWidthOrHeight = 0.5f;
        // 标题
        MakeText(canvas.transform, "Title", new Vector2(0, 230), 34, "弹弹堂 - 登录");
        // 服务器地址
        MakeText(canvas.transform, "ServerLabel", new Vector2(-120, 160), 20, "服务器:");
        serverInput_ = MakeInput(canvas.transform, "ServerInput", new Vector2(60, 160), loginHost);
        serverInput_.GetComponent<RectTransform>().sizeDelta = new Vector2(280, 36);
        // 账号
        MakeText(canvas.transform, "NameLabel", new Vector2(-120, 90), 20, "账号:");
        nameInput_ = MakeInput(canvas.transform, "NameInput", new Vector2(60, 90), "tester");
        // 密码
        MakeText(canvas.transform, "PwdLabel", new Vector2(-120, 30), 20, "密码:");
        pwdInput_ = MakeInput(canvas.transform, "PwdInput", new Vector2(60, 30), "1234");
        pwdInput_.inputType = InputField.InputType.Password;
        // 按钮
        var loginBtn = MakeButton(canvas.transform, "LoginBtn", new Vector2(-60, -40), "登录", OnLoginClick);
        var regBtn = MakeButton(canvas.transform, "RegBtn", new Vector2(80, -40), "注册", OnRegisterClick);
        // 退出游戏(右上角)
        var quitBtn = MakeButton(canvas.transform, "QuitBtn", new Vector2(560, 320), "退出游戏", OnQuitClick);
        quitBtn.GetComponent<RectTransform>().sizeDelta = new Vector2(100, 36);
        // 状态
        statusText_ = MakeText(canvas.transform, "Status", new Vector2(0, -110), 18, "");
    }

    public void OnQuitClick() {
        Application.Quit();
    }

    // ---- 按钮回调 ----
    public void OnLoginClick() {
        if (connecting_) return;
        connecting_ = true;
        statusText_.text = "登录中...";
        statusText_.color = Color.white;
        ApplyServerAddress();
        StartCoroutine(DoLogin());
    }

    // 从输入框读取服务器地址(支持 "host" 或 "host:port" 格式), 覆盖 loginHost/gateHost
    private void ApplyServerAddress() {
        if (serverInput_ == null || string.IsNullOrEmpty(serverInput_.text)) return;
        string addr = serverInput_.text.Trim();
        int colon = addr.LastIndexOf(':');
        if (colon > 0) {
            string host = addr.Substring(0, colon);
            if (int.TryParse(addr.Substring(colon + 1), out int port)) {
                loginHost = host; loginPort = port;
                gateHost = host; gatePort = port - 100;   // 约定: gate = login 端口 - 100
                return;
            }
        }
        loginHost = addr; gateHost = addr;
    }

    private System.Collections.IEnumerator DoLogin() {
        string name = nameInput_ != null ? nameInput_.text : "tester";
        string pwd = pwdInput_ != null ? pwdInput_.text : "1234";
        string token = null;
        DBGLogT("Login", $"DoLogin START name={name} loginHost={loginHost}:{loginPort} gateHost={gateHost}:{gatePort}");
        statusText_.text = "登录中(HTTP)...";
        yield return LoginClient.Login(loginHost, loginPort, name, pwd, r => {
            if (r.ok) { token = r.token; Session.MyAccountId = (ulong)r.account_id; Session.Token = token; }
            else { statusText_.text = "HTTP 登录失败: " + r.msg; statusText_.color = Color.red; connecting_ = false; }
        });
        if (string.IsNullOrEmpty(token)) {
            DBGWarn("[Login] DoLogin HTTP login failed (empty token)");
            yield break;
        }
        DBGLogT("Login", $"DoLogin HTTP ok, accountId={Session.MyAccountId}, connecting gate...");
        statusText_.text = "连接网关...";
        NetworkManager.Instance.Client.Connect(gateHost, gatePort);
        float t = 3f;
        while (!NetworkManager.Instance.Client.Connected && t > 0) { t -= Time.deltaTime; yield return null; }
        if (!NetworkManager.Instance.Client.Connected) {
            DBGWarn("[Login] DoLogin gate connect timeout (3s)");
            statusText_.text = "连接网关超时"; statusText_.color = Color.red; connecting_ = false; yield break;
        }
        DBGLogT("Login", "DoLogin gate connected, sending LOGIN frame");
        GameFacade.SendLogin(token);
    }

    public void OnRegisterClick() {
        statusText_.text = "注册中...";
        statusText_.color = Color.white;
        ApplyServerAddress();
        string name = nameInput_ != null ? nameInput_.text : "tester";
        string pwd = pwdInput_ != null ? pwdInput_.text : "1234";
        StartCoroutine(LoginClient.Register(loginHost, loginPort, name, pwd, (ok, id, msg) => {
            if (ok) {
                statusText_.text = $"注册成功 id={id}, 请登录";
                statusText_.color = Color.green;
            }
            else { statusText_.text = "注册失败: " + msg; statusText_.color = Color.red; }
        }));
    }

    // ---- UI 工具 ----
    private static Text MakeText(Transform parent, string name, Vector2 pos, int size, string content) {
        var go = new GameObject(name);
        go.transform.SetParent(parent, false);
        go.AddComponent<CanvasRenderer>();
        var t = go.AddComponent<Text>();
        t.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
        t.fontSize = size; t.alignment = TextAnchor.MiddleCenter; t.color = Color.white;
        t.raycastTarget = false; t.text = content;
        var rt = t.rectTransform; rt.sizeDelta = new Vector2(300, 40);
        rt.anchorMin = rt.anchorMax = new Vector2(0.5f, 0.5f); rt.anchoredPosition = pos;
        return t;
    }
    // ---- UI 工具 ----
    // MakeInput 暴露给其它 UI(LobbyUI) 复用
    public static InputField MakeInputPublic(Transform parent, string name, Vector2 pos, string placeholder) {
        return MakeInput(parent, name, pos, placeholder);
    }
    private static InputField MakeInput(Transform parent, string name, Vector2 pos, string placeholder) {
        var go = new GameObject(name);
        go.transform.SetParent(parent, false);
        go.AddComponent<CanvasRenderer>();
        var img = go.AddComponent<Image>(); img.color = new Color(1, 1, 1, 0.3f);
        var rt = go.GetComponent<RectTransform>(); rt.sizeDelta = new Vector2(200, 36);
        rt.anchorMin = rt.anchorMax = new Vector2(0.5f, 0.5f); rt.anchoredPosition = pos;
        var input = go.AddComponent<InputField>();
        // 文本
        var txtGo = new GameObject("Text"); txtGo.transform.SetParent(go.transform, false);
        txtGo.AddComponent<CanvasRenderer>();
        var txt = txtGo.AddComponent<Text>();
        txt.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf"); txt.fontSize = 20; txt.color = Color.white;
        txt.alignment = TextAnchor.MiddleCenter; txt.raycastTarget = false;
        txt.rectTransform.anchorMin = Vector2.zero; txt.rectTransform.anchorMax = Vector2.one;
        txt.rectTransform.offsetMin = new Vector2(4, 2); txt.rectTransform.offsetMax = new Vector2(-4, -2);
        input.textComponent = txt;
        // placeholder
        var phGo = new GameObject("Placeholder"); phGo.transform.SetParent(go.transform, false);
        phGo.AddComponent<CanvasRenderer>();
        var ph = phGo.AddComponent<Text>();
        ph.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf"); ph.fontSize = 18;
        ph.fontStyle = FontStyle.Italic; ph.color = new Color(1,1,1,0.4f); ph.text = placeholder;
        ph.alignment = TextAnchor.MiddleCenter;
        ph.rectTransform.anchorMin = Vector2.zero; ph.rectTransform.anchorMax = Vector2.one;
        ph.rectTransform.offsetMin = new Vector2(4,2); ph.rectTransform.offsetMax = new Vector2(-4,-2);
        input.placeholder = ph;
        return input;
    }
    private static Button MakeButton(Transform parent, string name, Vector2 pos, string label, UnityEngine.Events.UnityAction onClick) {
        var go = new GameObject(name);
        go.transform.SetParent(parent, false);
        go.AddComponent<CanvasRenderer>();
        var img = go.AddComponent<Image>(); img.color = new Color(0.25f, 0.5f, 0.9f, 1f);
        var rt = go.GetComponent<RectTransform>(); rt.sizeDelta = new Vector2(110, 40);
        rt.anchorMin = rt.anchorMax = new Vector2(0.5f, 0.5f); rt.anchoredPosition = pos;
        var btn = go.AddComponent<Button>();
        var colors = btn.colors; colors.highlightedColor = new Color(0.4f, 0.7f, 1f); btn.colors = colors;
        // label
        var lblGo = new GameObject("Label"); lblGo.transform.SetParent(go.transform, false);
        lblGo.AddComponent<CanvasRenderer>();
        var lbl = lblGo.AddComponent<Text>();
        lbl.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf"); lbl.fontSize = 22;
        lbl.alignment = TextAnchor.MiddleCenter; lbl.color = Color.white; lbl.text = label; lbl.raycastTarget = false;
        lbl.rectTransform.anchorMin = Vector2.zero; lbl.rectTransform.anchorMax = Vector2.one; lbl.rectTransform.offsetMin = lbl.rectTransform.offsetMax = Vector2.zero;
        btn.onClick.AddListener(onClick);
        return btn;
    }
}
}
