#include <Render/FrameStats.h>

#include <GLFW/glfw3.h>

void FrameStats::Init()
{
    glGenQueries(QUERY_COUNT, queries);
}

void FrameStats::Shutdown()
{
    if (queries[0] != 0)
    {
        glDeleteQueries(QUERY_COUNT, queries);
        queries[0] = 0;
    }
}

void FrameStats::BeginFrame()
{
    drawCalls = 0;
    triangles = 0;

    prevFrameStartTime = frameStartTime;
    frameStartTime = glfwGetTime();

    //직전 프레임에서 모달 창이 떴다면 그 간격은 렌더 성능이 아니라 사람이 파일을 고른 시간이다.
    //첫 프레임과 똑같이 취급해서(기준점 없음) frameMs를 갱신하지 않고 넘긴다.
    if (skipNextDelta)
    {
        prevFrameStartTime = 0.0;
        skipNextDelta = false;
    }

    //FPS는 "프레임 시작에서 다음 프레임 시작까지"로 재야 맞다.
    //cpuMs로 계산하면 vsync 대기 시간이 빠져서 실제보다 훨씬 높게 나온다.
    if (prevFrameStartTime > 0.0)
        frameMs = (float)((frameStartTime - prevFrameStartTime) * 1000.0);

    //이번 프레임에 쓸 쿼리 슬롯. GL_TIME_ELAPSED는 동시에 하나만 활성화될 수 있어서
    //Begin/End 쌍을 프레임마다 슬롯 바꿔가며 쓴다.
    glBeginQuery(GL_TIME_ELAPSED, queries[frameIndex]);
    queryActive[frameIndex] = true;
}

void FrameStats::EndFrame()
{
    glEndQuery(GL_TIME_ELAPSED);

    lastDrawCalls = drawCalls;
    lastTriangles = triangles;

    //CPU 시간: 이 프레임의 명령을 쌓는 데 걸린 시간. GPU 시간과 따로 봐야 병목이 어디인지 알 수 있다.
    //(CPU만 높으면 드로우콜/상태변경 과다, GPU만 높으면 픽셀/버텍스 부하)
    //멈춘 프레임이면 직전 값을 그대로 유지한다 — 측정할 수 없었던 것이지 0인 게 아니다.
    if (stalledThisFrame)
    {
        stalledThisFrame = false;
        skipNextDelta = true;
    }
    else
    {
        cpuMs = (float)((glfwGetTime() - frameStartTime) * 1000.0);
    }

    //가장 오래된 슬롯 = 다음에 덮어쓸 슬롯. 지금 읽으면 GPU가 이미 끝냈을 확률이 높다.
    int oldest = (frameIndex + 1) % QUERY_COUNT;
    if (queryActive[oldest])
    {
        //혹시 아직 안 끝났으면 이번 프레임은 그냥 건너뛴다 — 기다리면 스톨이라 계측기가 계측 대상을 망침
        GLint available = 0;
        glGetQueryObjectiv(queries[oldest], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available)
        {
            GLuint64 elapsedNs = 0;
            glGetQueryObjectui64v(queries[oldest], GL_QUERY_RESULT, &elapsedNs);
            gpuMs = (float)(elapsedNs / 1000000.0);
            queryActive[oldest] = false;
        }
    }

    frameIndex = (frameIndex + 1) % QUERY_COUNT;

    //지수이동평균(EMA): 새 값을 10%만 섞어서 숫자가 덜 튀게 한다
    float instantFps = frameMs > 0.0f ? 1000.0f / frameMs : 0.0f;
    fps = (fps == 0.0f) ? instantFps : (fps * 0.9f + instantFps * 0.1f);
}
