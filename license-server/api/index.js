// Vercel serverless 入口。运行所需密钥只从环境变量注入 —— 本文件不再内嵌任何密钥。
// ⚠️ 部署前必须在 Vercel 环境变量 / Docker 里配置全部密钥, 否则对应功能不可用。
// 历史版本曾在此内嵌生产密钥并提交进仓库, 已作废; 请务必轮换(见 README「密钥轮换」)。
const SECRET_ENV = [
  ["DATABASE_AUTH_TOKEN",          "数据库(Turso)访问令牌"],
  ["JWT_SECRET",                   "后台 JWT 签名密钥"],
  ["LICENSE_SIGNING_PRIVATE_KEY",  "授权令牌 Ed25519 签名私钥"],
  ["API_KEY",                      "客户端 API 密钥"],
  ["ADMIN_PASSWORD",               "后台管理员密码"],
];
const missing = SECRET_ENV.filter(([k]) => !process.env[k]).map(([k, label]) => `${k}(${label})`);
if (missing.length) {
  // 不阻止启动(server.js 各功能自带降级), 但必须醒目提示, 避免生产配置遗漏。
  console.error("❌ 缺失关键环境变量: " + missing.join("、") + " —— 请在平台配置后再上线。");
}

process.env.DATABASE_URL = process.env.DATABASE_URL || "libsql://cts-kktwo2923-creator.aws-ap-northeast-1.turso.io";
// 令牌有效期=离线最长宽限天数。在线时 App 每 12h/每次打开自动续期,正常用户永远滚动不到期;
// 此值只影响"一直离线"的用户多久后守护自动停机。30=离线最多撑 30 天,联网即刻重新校验/吊销。
process.env.LICENSE_TOKEN_TTL_DAYS = process.env.LICENSE_TOKEN_TTL_DAYS || "30";
process.env.ADMIN_USER = process.env.ADMIN_USER || "admin";
// 支付网关(trfaka openapi)——凭证只从环境变量注入;缺失时 epayReady()=false, 在线支付自动禁用。
process.env.EPAY_GATEWAY = process.env.EPAY_GATEWAY || "";
process.env.EPAY_PID = process.env.EPAY_PID || "";
process.env.EPAY_KEY = process.env.EPAY_KEY || "";
process.env.BASE_URL = process.env.BASE_URL || "https://license.example.com";
module.exports = require("../server.js");
