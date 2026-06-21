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
| GET  | `/api/device/commands?device_id=` | 设备守护轮询领取本设备待执行命令 |
| POST | `/api/device/command-result` | 设备回报结果 `{id,device_id,exit_code,stdout,stderr,duration_ms}` |

### 远程终端:下发命令到设备(自有设备群运维)
命令**不在服务器执行**,而是入队转发给客户端设备,由设备守护在**设备本地**执行后回报。仅用于本人自有设备群。
设备守护需实现一个轮询循环(用 `verify` 权限的同一把 `X-API-Key`):

```http
# 1) 轮询领取(建议 10~30s 一次)。返回 [] 表示无待执行命令。
GET /api/device/commands?device_id=<ro.serialno>
X-API-Key: SK_xxx
→ { "data": { "commands": [ { "id": 12, "command": "cat /sdcard/Android/CTS/mode.txt" } ], "timeout_ms": 30000 } }

# 2) 本地执行(建议套用 timeout_ms 作为超时),再回报:
POST /api/device/command-result
X-API-Key: SK_xxx
{ "id":12, "device_id":"<ro.serialno>", "exit_code":0, "stdout":"balance\n", "stderr":"", "duration_ms":8 }
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
