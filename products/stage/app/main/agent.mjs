// The built-in agent: one writer, two tools, on the AI SDK's own loop with no framework. It only
// ever writes one file, whose path the app chose. Every save plays the file and hands back the
// checks and a few frames, so the model sees what it made.
import { ToolLoopAgent, tool, isStepCount } from "ai";
import { createAnthropic } from "@ai-sdk/anthropic";
import { z } from "zod";
import fs from "node:fs/promises";

export const SCENE_JOB = `
# Your job here

You are the scene writer inside Stage. The person typed one request. Write one scene file that does it, following the contract above.

Call \`write\` with the complete file. Stage saves it, plays it against a drum pattern you have not seen, and returns the checks and four frames from one bar: just after a kick, just after a snare, a quiet moment, and the end of the bar. Look at the frames and judge whether they show what was asked for.

If a check fails or the picture is wrong, fix it with \`edit\`, or with \`write\` for a large change. When the checks pass and the picture is right, stop and reply with one short sentence saying what you made.

The checks: \`lint\` lists banned names found in the file. \`errors\` lists what the scene threw. \`blank\` means nothing was drawn. \`sameTwice\` means two replays gave identical frames. \`followsDrums\` means the picture moves when the drums hit, wherever they hit.`;

export const CUT_JOB = `
# Your job here

You are the Cut writer inside Stage. The person selected two scenes and typed one request. Write the Cut between them, following the contract above.

Call \`write\` with the complete file. Stage compiles it, plays it between the two scenes, and returns the checks and five frames, at progress 0.1, 0.3, 0.5, 0.7 and 0.9. Look at the frames and judge whether they show what was asked for, and whether the cut starts on the outgoing picture and ends on the incoming one.

If a check fails or the picture is wrong, fix it with \`edit\`, or with \`write\` for a large change. When the checks pass and the picture is right, stop and reply with one short sentence saying what you made.

The checks: \`lint\` lists what the file must not contain. \`errors\` lists what the shader compiler said; its line numbers count from the start of Stage's whole shader, not of your file. \`changesAcrossTheCut\` means the five frames all differ. \`sameTwice\` means two replays gave identical frames.`;

// The key comes from ANTHROPIC_API_KEY. The address is fixed here because the environment may set
// ANTHROPIC_BASE_URL without the /v1 this provider expects.
const anthropic = createAnthropic({ baseURL: "https://api.anthropic.com/v1" });

// Writes or changes one file. \`instructions\` is the contract plus the job; \`field\` names the
// tool input that carries the file ("html" for a scene, "wgsl" for a Cut).
export async function writeFile({ model, instructions, field, request, file, url, existing, check, onEvent = () => {} }) {
  const attempts = [];
  const started = Date.now();
  // Where the time goes, in seconds from the start: each save, each check, each model turn.
  const trace = [];
  const mark = (what) => { trace.push([Number(((Date.now() - started) / 1000).toFixed(1)), what]); onEvent({ seconds: trace.at(-1)[0], what }); };
  let saved = existing ?? "";
  // A model may call several tools in one turn. Plays run one at a time, and a play of text that
  // was just played returns that result, so two edits in a turn cost one check of the final file.
  let last = Promise.resolve({ text: null });
  let pictures = [];
  const play = () => (last = last.then(async (previous) => {
    if (previous.text === saved) return previous;
    const text = saved;
    await fs.writeFile(file, text);
    mark("saved, checking");
    const result = await check(`${url}?v=${attempts.length}`, text);
    mark(result.report.passed ? "checked: pass" : "checked: fail");
    attempts.push(result.report);
    if (result.pictures.length) pictures = result.pictures;
    return { ...result, text };
  }));
  const toModelOutput = ({ output }) => ({
    type: "content",
    value: [{ type: "text", text: JSON.stringify(output.report) }, ...output.pictures.map((data) => ({ type: "file", mediaType: "image/jpeg", data: { type: "data", data } }))],
  });
  const agent = new ToolLoopAgent({
    model: anthropic(model),
    instructions,
    maxOutputTokens: 32000,
    stopWhen: isStepCount(8),
    onStepEnd: ({ usage }) => mark(`model turn done, ${usage.outputTokens} tokens out`),
    tools: {
      write: tool({
        description: "Save the whole file and play it. Returns the checks and frames.",
        inputSchema: z.object({ [field]: z.string() }),
        execute: (input) => { saved = input[field]; return play(); },
        toModelOutput,
      }),
      edit: tool({
        description: "Replace one exact piece of the saved file, then play it. `find` must match exactly once.",
        inputSchema: z.object({ find: z.string(), replace: z.string() }),
        execute: ({ find, replace }) => {
          const matches = saved.split(find).length - 1;
          if (matches !== 1) return { report: { error: `find matched ${matches} times; it must match exactly once` }, pictures: [] };
          saved = saved.replace(find, () => replace);
          return play();
        },
        toModelOutput,
      }),
    },
  });
  // A model call that stalls must not hang the app either: five minutes for the whole job.
  const result = await agent.generate({
    prompt: existing ? `This is the file to change:\n\n${existing}\n\nThe change asked for: ${request}` : request,
    abortSignal: AbortSignal.timeout(300000),
  });
  return {
    passed: attempts.at(-1)?.passed ?? false, attempts, steps: result.steps.length, seconds: Number(((Date.now() - started) / 1000).toFixed(1)),
    usage: result.usage, finish: result.finishReason, said: result.text, bytes: saved.length, trace, pictures,
  };
}
