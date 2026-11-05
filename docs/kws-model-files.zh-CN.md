# sherpa-onnx KWS 模型文件格式

> 对象：`asr` 的 `backend: keyword`
> 适用模型：`sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20`（以模型 README 为准）
> 相关配置：`keyword.tokens`、`keyword.keywords_file`

`keyword` 后端使用 sherpa-onnx 的 `KeywordSpotter`。它需要两个完全不同的文件：

| 文件 | 归属 | 格式 | 是否手改 |
|---|---|---|---|
| `tokens.txt` | 模型 | `token id` | **不要手改** |
| `keywords_raw.txt` | 用户 | `原始词 @显示词` | 手写 |
| `keywords.txt` | 用户/工具 | `token token ... @显示词` | 由 `text2token` 生成 |

---

## 1. `tokens.txt`

`tokens.txt` 是声学模型的 token 表，每行一个 token 和整数 id：

```text
<blk> 0
<sos/eos> 1
<unk> 2
AA0 3
AA1 4
AA2 5
...
w 257
én 258
ǚ 259
ǜ 260
ǹ 261
ḿ 262
```

本模型共 263 行。前三个是特殊 token：

| token | 含义 |
|---|---|
| `<blk>` | blank，CTC/transducer 的空白符号 |
| `<sos/eos>` | 句子开始/结束 |
| `<unk>` | 未知符号 |

其余 token 是模型训练时使用的 phone、拼音或 BPE 单元，可能包含声调符号、重音数字，例如：

```text
AY1
zh
én
uǒ
```

规则：

- 每行必须是 `token id`，用空格分隔；
- id 必须与模型匹配；
- token 由模型训练决定，不能为了新增关键词而修改；
- 替换模型时，`tokens.txt` 必须一起替换。

---

## 2. `keywords_raw.txt`（人类可读输入）

`keywords_raw.txt` 不是给 `KeywordSpotter` 直接读取的，而是给 `text2token` 生成 `keywords.txt` 的输入。

格式：

```text
<原始词> @<显示词>
```

示例：

```text
LIGHT UP @LIGHT_UP
LOVELY CHILD @LOVELY_CHILD
文森特卡索 @文森特卡索
周望军 @周望军
女儿 @女儿
法国 @法国
小鸭小鸭 @小鸭小鸭
```

规则：

- 每行一个关键词；
- `@` 后面是最终返回给业务的显示词；
- 显示词**不能包含空格**；英文用下划线，例如 `LIGHT_UP`；
- 原始词可以包含空格，但生成工具会按模型规则切分；
- 不要写空行或注释行。

---

## 3. `keywords.txt`（`KeywordSpotter` 实际读取）

格式：

```text
<token1> <token2> ... @<显示词>
```

模型自带示例：

```text
L AY1 T AH1 P @LIGHT_UP
L AH1 V L IY0 CH AY1 L D @LOVELY_CHILD
w én s ēn t è k ǎ s uǒ @文森特卡索
zh ōu w àng j ūn @周望军
zh ū l ì n án @朱丽楠
j iǎng y ǒu b ó @蒋友伯
n ǚ ér @女儿
f ǎ g uó @法国
j iàn m iàn h uì @见面会
l uò sh í @落实
```

规则：

- `@` 前是关键词的 token 序列；每个 token 必须存在于 `tokens.txt`；
- token 之间用空格分隔；
- `@` 后是显示词，`KeywordResult.keyword` 返回这个值；
- 显示词不能有空格；
- 一行一个关键词；
- **不能直接写“文森特卡索”**，除非模型本身就是字符级/BPE 词表；本模型需要拼音/phone token；
- 手改 `keywords.txt` 容易写错 token，建议只维护 `keywords_raw.txt`，用工具生成。

### 本模型如何生成

本模型（`kws-zipformer-zh-en-3M-2025-12-20`）使用 `phone+ppinyin`：

```bash
sherpa-onnx-cli text2token \
  --tokens /opt/sherpa-onnx/models/kws/tokens.txt \
  --tokens-type phone+ppinyin \
  --lexicon /opt/sherpa-onnx/models/kws/en.phone \
  keywords_raw.txt keywords.txt
```

如果当前安装方式没有 `sherpa-onnx-cli`，使用 sherpa-onnx 源码中的 `scripts/text2token.py`，参数含义相同。

注意：`--tokens-type` 必须和模型训练配置一致。其他模型可能是 `cjkchar` 或 `bpe`，不能直接套用本模型的 `phone+ppinyin`；以模型 README 为准。

### 一个中文关键词的生成结果

输入：

```text
小鸭小鸭 @小鸭小鸭
打开电量 @打开电量
```

生成结果会是一串本模型 `tokens.txt` 中存在的拼音/phone token，而不是汉字本身，形如：

```text
<token> <token> ... @小鸭小鸭
<token> <token> ... @打开电量
```

具体 token 序列由 `text2token` 和模型词表决定，不要手工猜测。

---

## 4. 配置

```yaml
backend: keyword

keyword:
  encoder: /opt/sherpa-onnx/models/kws/encoder-epoch-13-avg-2-chunk-16-left-64.int8.onnx
  decoder: /opt/sherpa-onnx/models/kws/decoder-epoch-13-avg-2-chunk-16-left-64.onnx
  joiner: /opt/sherpa-onnx/models/kws/joiner-epoch-13-avg-2-chunk-16-left-64.int8.onnx
  tokens: /opt/sherpa-onnx/models/kws/tokens.txt
  keywords_file: /opt/sherpa-onnx/models/kws/keywords.txt
  max_active_paths: 4
  num_trailing_blanks: 1
  keywords_score: 1.0
  keywords_threshold: 0.25
```

`tokens` 和 `keywords_file` 必须是不同的文件。

---

## 5. 后端如何使用结果

`KeywordBackend` 只读取 `SherpaOnnxKeywordResult.keyword`：

```cpp
event.type = BackendEventType::Final;
event.text = result->keyword;  // keywords.txt 中 @ 后的显示词
```

`AsrNode` 把它转换成：

```text
AsrEvent.event = EVENT_FINAL
AsrEvent.text  = <显示词>
AsrEvent.language = ""（KWS 不提供语言标签）
```

因此上层（例如 `duck_asr`）直接按配置里的显示词做匹配即可。

---

## 6. 常见问题

| 现象 | 检查 |
|---|---|
| 模型加载失败 | 四个模型文件路径是否正确；`tokens.txt` 是否与模型匹配 |
| 能加载但从不触发 | `keywords.txt` 是否为空；token 是否存在于 `tokens.txt`；`keywords_threshold`/`keywords_score` 是否合适 |
| 触发但文本为空 | 关键词行是否缺少 `@显示词`；显示词是否为空 |
| 中文关键词不识别 | 是否直接用汉字写了 `keywords.txt`；应通过 `text2token` 生成 |
| 英文关键词返回带空格 | `@` 后应使用下划线形式，如 `LIGHT_UP` |
| 修改关键词不生效 | 是否改了 `keywords_raw.txt` 但没有重新生成 `keywords.txt` |

模型文件与关键词文件都不随 `asr` 包分发；部署时按 `README` 的下载步骤准备。
