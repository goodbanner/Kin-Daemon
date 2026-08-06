# OnePlus V Pro · 卡密授权系统(后端 + 网页后台)

设备绑定(`ro.serialno`)的卡密授权服务:随机卡密生成、首次激活绑定设备、服务端验证、网页管理后台。
验证成功会签发 HMAC 令牌,供模块守护进程**离线兜底校验**(后续接入)。

## 能力
- 随机卡密生成(数量/有效天数(0=永久)/前缀/产品/备注)
- 一机一码:首次验证绑定 `device_id`(= 设备序列号),换设备需管理员解绑
- 有效期管理、禁用/启用/解绑/删除、统计、验证日志
- 网页后台(登录→生成/列表/操作)+ REST API(`X-API-Key`)
- 请求限流(验证 30 次/5 分钟,其它 100 次/15 分钟)

## 推荐部署:Koyeb(常驻不休眠、免信用卡)+ Turso(5GB 永久免费、免信用卡、数据持久)
> Koyeb 容器磁盘是临时的,**生产必须用 Turso 外接库**,否则重新部署会丢卡密数据。

### 1) 建 Turso 数据库(免信用卡)
1. 注册 https://turso.tech 。
2. 控制台新建 Database,记下:
   - 数据库 URL,形如 `libsql://xxx-yourname.turso.io`
   - Auth Token(Create Token)。

### 2) 代码放到 GitHub 私有仓库
把本目录所有文件传到一个**私有**仓库(Koyeb 从 GitHub 拉取构建)。

### 3) Koyeb 部署
1. 注册 https://www.koyeb.com (可 GitHub 登录,免信用卡)。
2. Create Service → GitHub → 选该仓库;Koyeb 会识别 `Dockerfile`。
3. 实例选 Free;端口 `3000`(Dockerfile 已 EXPOSE)。
4. 添加环境变量:
   - `DATABASE_URL` = `libsql://xxx-yourname.turso.io`
   - `DATABASE_AUTH_TOKEN` = 你的 Turso Token
   - `JWT_SECRET` = 32 位随机串(`openssl rand -hex 32`)
   - `LICENSE_SIGNING_SECRET` = 32 位随机串(**记下来,接入守护时要用同一个**)
   - `ADMIN_PASSWORD` = 你的后台强密码
   - `API_KEY` = 自定义客户端密钥(留空则首次部署日志里随机打印一次)
5. Deploy。完成后访问 Koyeb 给的域名 `https://xxx.koyeb.app`。

### 4) 使用
- 打开域名 → 网页后台 → 用 `admin / 你设的密码` 登录 → 生成卡密。
- 客户端(App/守护)调用 API 时带 `X-API-Key: 你的API_KEY`。

> 备选:Render(需信用卡)同理,Build `npm install`、Start `node init-db.js && node server.js`,环境变量相同。

## 本地自测
```bash
npm install
npm run init-db   # 打印管理员账号 + API 密钥(保存)
npm start         # http://localhost:3000
```
默认用 `file:local.db` 本地 SQLite。

## API 速览(均需 `X-API-Key`,管理类也接受后台登录的 Bearer JWT)
| 方法 | 路径 | 说明 |
|---|---|---|
| GET  | `/api/health` | 健康检查 |
| POST | `/api/verify` | 客户端验证卡密(绑定设备、返回令牌) |
| POST | `/api/license/generate` | 生成卡密 `{count,duration_days,product_id,prefix,note}` |
| GET  | `/api/license/list?status=&page=&limit=` | 列表 |
| POST | `/api/license/disable` / `enable` / `unbind` / `delete` | `{license_key}` |
| GET  | `/api/stats` | 统计 + 最近验证日志 |
| POST | `/api/admin/login` | 后台登录 `{username,password}`→JWT |
| POST | `/api/admin/shell` | 后台下发命令到设备 `{device_id,command}`(广播填 `device_id:"*"`) |
| GET  | `/api/admin/shell/logs?device_id=&limit=` | 命令下发/回报历史(审计) |
| POST | `/api/device/command-result` | 设备回报结果 `{id,device_id,exit_code,stdout,stderr,duration_ms}` |

### 远程终端:下发命令到设备(仅限自有设备、非破坏运维)
> ⚠️ 仅用于**你自己拥有**的设备群做非破坏运维(切模式、拉日志、重置配置等)。
> **禁止**用于向第三方/疑似盗版设备下发删除数据等破坏性命令——那是违法且会误伤真实用户的。

命令**不在服务器执行**,而是入队后**随该设备下次 `/api/verify` 响应一起下发**(客户端本就会定期验证,无需单独轮询)。
取走即标记 `sent`(单次投递),客户端在**设备本地**执行后回报结果 → `done`/`error`。

**客户端 App(调用 /api/verify 的那一端)需要补这段逻辑**才会生效——服务器只是通道:

```js
// 客户端拿到 /api/verify 的响应后:
const cmds = resp.data.pending_commands || [];      // [{ id, command }]
for (const c of cmds) {
  const { code, out, err, ms } = runLocally(c.command);   // 在设备本地执行(设好超时)
  await post("/api/device/command-result", {              // 用同一把 verify 的 X-API-Key
    id: c.id, device_id: mySerial,
    exit_code: code, stdout: out, stderr: err, duration_ms: ms
  });
}
```
被后台拉黑(黑名单)的设备不会收到任何命令。环境变量 `ENABLE_SHELL=0` 可全局关闭下发。

### 验证请求/响应
```http
POST /api/verify
X-API-Key: SK_xxx
{ "license_key":"KEY-....", "device_id":"<ro.serialno>", "device_info":"机型/版本" }
```
```json
{ "code":200,"success":true,"data":{
  "expires_at":"...","remaining_days":30,
  "token":"<hmac>"   // 模块守护离线兜底校验用
}}
```

## 状态码
0 未使用 · 1 已激活 · 2 已过期 · 3 已禁用

## 防休眠(若用会休眠的平台)
用 https://uptimerobot.com 每 5 分钟 ping `/api/health`。Koyeb Free 常驻一般不休眠,可不配。
