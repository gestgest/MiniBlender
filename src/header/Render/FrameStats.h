#pragma once

#include <glad/glad.h>

//프레임 계측기. 이 프로젝트의 목표가 "드로우콜 기술 넣기"라서, 넣기 전에 재는 수단부터 있어야 한다.
//기법을 넣고 나서 "빨라진 것 같다"가 아니라 "드로우콜 500 -> 1, GPU 3.2ms -> 0.8ms"로 말할 수 있어야 함.
//
//GPU 시간 측정이 까다로운 이유:
//  CPU가 glDrawArrays를 호출한 시점과 GPU가 실제로 그리는 시점이 다르다(비동기).
//  그래서 CPU에서 시계를 재면 "명령 넣는 데 걸린 시간"만 나오지 실제 렌더 시간이 안 나온다.
//  GL_TIME_ELAPSED 쿼리는 GPU 타임라인 위에 직접 스톱워치를 꽂는 방식이라 진짜 GPU 시간이 나온다.
//
//다만 결과를 그 프레임에 바로 물어보면(glGetQueryObject) GPU가 끝날 때까지 CPU가 멈춰버린다(파이프라인 스톨).
//그래서 쿼리를 여러 개 돌려쓰면서 N프레임 전 결과를 읽는다. 그때쯤이면 GPU가 이미 끝냈으니 안 기다려도 됨.
class FrameStats
{
public:
    void Init();
    void Shutdown();

    //프레임 시작 — GPU 타이머 켜고 카운터 리셋
    void BeginFrame();
    //프레임 끝 — GPU 타이머 끄고, 여유가 생긴 과거 쿼리 결과를 수거
    void EndFrame();

    //드로우콜 낼 때마다 불러준다. Renderer가 대신 호출해주니 직접 쓸 일은 별로 없음
    void AddDrawCall(unsigned int triangleCount)
    {
        drawCalls++;
        triangles += triangleCount;
    }

    unsigned int GetDrawCalls()  const { return lastDrawCalls; }
    unsigned int GetTriangles()  const { return lastTriangles; }
    float GetCpuMs()             const { return cpuMs; }     //명령 쌓는 데 든 시간
    float GetGpuMs()             const { return gpuMs; }     //GPU가 실제로 그린 시간
    float GetFrameMs()           const { return frameMs; }   //프레임 간 실제 간격 (vsync 대기 포함)
    float GetFps()               const { return fps; }

private:
    static const int QUERY_COUNT = 3;   //3프레임 정도 묵히면 스톨 없이 결과가 준비된다

    unsigned int queries[QUERY_COUNT] = { 0, 0, 0 };
    bool queryActive[QUERY_COUNT] = { false, false, false };
    int frameIndex = 0;

    //현재 프레임 누적 중인 값
    unsigned int drawCalls = 0;
    unsigned int triangles = 0;

    //화면에 보여줄 확정값 (프레임 끝에서 스냅샷)
    unsigned int lastDrawCalls = 0;
    unsigned int lastTriangles = 0;

    double frameStartTime = 0.0;
    double prevFrameStartTime = 0.0;
    float cpuMs = 0.0f;
    float gpuMs = 0.0f;
    float frameMs = 0.0f;

    //FPS는 매 프레임 출렁여서 읽기 힘드니 지수이동평균으로 부드럽게
    float fps = 0.0f;
};
