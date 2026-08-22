# MiniBlender

C++/OpenGL 4.6으로 만드는 미니 3D 에디터. 블렌더 같은 DCC 툴의 뼈대를 직접 구현하면서
**드로우콜 최적화 기법을 하나씩 넣어보는 게 목표**인 학습 프로젝트다.

기법을 넣기 전에 재는 수단부터 만들었다. 드로우콜 수, 삼각형 수, CPU/GPU 시간이 항상 화면에 떠 있어서
"인스턴싱을 넣었더니 드로우콜 500 → 1, GPU 3.2ms → 0.8ms"처럼 **숫자로 확인하면서** 진행한다.

## 빌드

Visual Studio 2022로 `MiniBlender.sln`을 열고 **x64 / Debug** 로 빌드하면 끝이다.
의존성(GLAD, GLFW, GLM, Dear ImGui)은 전부 저장소에 포함돼 있어서 별도 설치가 필요 없다.

| 의존성 | 버전 | 위치 |
|---|---|---|
| GLAD | OpenGL 4.6 Core (glad 0.1.36 생성) | `include/glad`, `src/glad.c` |
| GLFW | 3.x | `include/GLFW`, `lib/` |
| GLM | — | `include/glm` |
| Dear ImGui | v1.92.3 (docking) | `third_party/imgui` |

> 실행 시 작업 디렉터리는 **프로젝트 디렉터리**여야 한다 (`src/vs/*.vs` 같은 셰이더를 상대 경로로 읽는다).
> VS에서 F5로 실행하면 기본값이 그렇게 잡혀 있다.

## 조작

| 입력 | 동작 |
|---|---|
| 중간 버튼 드래그 | 궤도 회전 (orbit) |
| Shift + 중간 버튼 | 평행 이동 (pan) |
| 휠 | 줌 (거리에 비례) |
| ESC | 종료 |

## 측정 모드

인자를 주고 실행하면 UI 없이 지정한 씬을 그린 뒤, 결과 한 줄을 찍고 자동 종료한다.

```
MiniBlender.exe <개수> [메시이름] [워밍업프레임] [샘플프레임]
```

```bash
> MiniBlender.exe 10000 Cube
[벤치] Cube x10000 (워밍업 120프레임, 샘플 300프레임)
RESULT mesh=Cube n=10000 calls=10002 tris=120013 cpu=3.218 gpu=1.730 frame=3.384 fps=295.5
```

메시 이름은 `Cube`(기본) / `Plane` / `Sphere` / `Cylinder` / `HiSphere`.
`HiSphere`는 고해상도 구(16,128 삼각형)의 ASCII 별칭이다 —
argv가 콘솔 코드페이지로 들어와서 한글 메시 이름은 인자로 넘길 수 없다.

측정 중에는 **vsync를 끄고 UI를 그리지 않는다.** vsync가 켜져 있으면 3ms 프레임과 12ms 프레임이
똑같이 모니터 주사율에 맞춰져 구분이 안 되고, ImGui 렌더링 비용이 숫자에 섞이면 비교가 흐려진다.
워밍업 프레임을 버리는 것도 같은 이유다 — 초반에는 셰이더 컴파일과 버퍼 업로드로 값이 크게 튄다.

여러 조건을 한 번에 돌려 표를 만들 수 있다.

```powershell
foreach ($n in 100,500,1000,2000,5000,10000) { .\x64\Release\MiniBlender.exe $n Cube }
```

최적화 기법을 넣기 전후로 **같은 명령을 돌려 비교하는 것**이 이 프로젝트의 기본 작업 방식이다.
측정 결과는 [`docs/devlog/`](docs/devlog/)에 정리한다.

## 구조

```
src/
├── main.cpp                 창/GL 초기화, 렌더 루프, 입력
├── header/, cpp/
│   ├── Render/FrameStats    드로우콜·삼각형·CPU/GPU 시간 계측
│   ├── Render/Mesh          VAO/VBO/EBO + 프리미티브 생성기
│   ├── Render/OrbitCamera   DCC식 궤도 카메라
│   ├── Render/Renderer      씬 렌더링 (드로우콜 제출 지점)
│   ├── Scene/Scene          메시 라이브러리 + 오브젝트 목록
│   └── UI/EditorUI          ImGui 패널 (통계 / 아웃라이너 / 속성)
├── vs/, fs/                 셰이더
third_party/imgui/           Dear ImGui (vendored)
```

## 구현 메모

- **GPU 시간 측정**: `GL_TIME_ELAPSED` 쿼리를 3개 돌려쓰면서 3프레임 전 결과를 읽는다.
  당장 읽으면 GPU가 끝날 때까지 CPU가 멈춰서(파이프라인 스톨) 계측기가 계측 대상을 왜곡한다.
- **CPU/GPU 분리 표시**: CPU만 높으면 드로우콜·상태변경 병목(→ 배칭/인스턴싱),
  GPU만 높으면 픽셀·버텍스 병목(→ 컬링/LOD). 섞어 보면 병목을 못 찾는다.
- **무한 그리드**: 지오메트리 없이 `gl_VertexID`로 만든 풀스크린 삼각형 1개에서
  광선-평면 교차를 풀어 격자를 그린다. 드로우콜 1개, `fwidth` 기반 안티에일리어싱.
- **DSA 사용**: `glCreateBuffers` / `glNamedBufferStorage` 등. 바인딩 상태에 의존하지 않아
  순서 실수로 인한 버그가 원천 차단된다.
- **디버그 출력**: Debug 빌드에서 `glDebugMessageCallback`을 붙여 GL 에러를 콘솔에 즉시 출력한다.

## 로드맵

- [x] **1단계 — 뼈대**: 메시/씬 구조, 궤도 카메라, 무한 그리드, 프레임 계측
- [ ] **2단계 — 선택/조작**: ID 버퍼 피킹(FBO + `R32UI`), 스텐실 아웃라인, 트랜스폼 기즈모
- [ ] **3단계 — 드로우콜 최적화**: 상태 정렬 → UBO → 인스턴싱 → 프러스텀 컬링 →
      `glMultiDrawElementsIndirect` + 버텍스 풀링 → compute 기반 GPU 컬링
- [ ] **4단계 — 셰이딩**: 디퍼드/포워드+, PBR, 그림자 맵, SSAO, HDR 톤매핑, MSAA

## 라이선스

Dear ImGui는 MIT 라이선스이며 `third_party/imgui/LICENSE.txt`에 원문이 있다.
