# MiniBlender
<img width="1284" height="752" alt="image" src="https://github.com/user-attachments/assets/fc5742e2-9605-4ff5-ad79-ed8ccf675c2c" />

C++/OpenGL 4.6으로 만드는 미니 3D 에디터. 블렌더 같은 DCC 툴의 뼈대를 직접 구현하면서
**드로우콜 최적화 기법을 하나씩 넣어보는 게 목표**인 학습 프로젝트다.

기법을 넣기 전에 재는 수단부터 만들었다. 드로우콜 수, 삼각형 수, CPU/GPU 시간이 항상 화면에 떠 있어서
"인스턴싱을 넣었더니 드로우콜 500 → 1, GPU 3.2ms → 0.8ms"처럼 **숫자로 확인하면서** 진행한다.

## 빌드

Visual Studio 2022로 `MiniBlender.sln`을 열고 **x64 / Debug** 로 빌드하면 끝이다.
의존성(GLAD, GLFW, GLM, Dear ImGui, ufbx)은 전부 저장소에 포함돼 있어서 별도 설치가 필요 없다.

| 의존성 | 버전 | 위치 |
|---|---|---|
| GLAD | OpenGL 4.6 Core (glad 0.1.36 생성) | `include/glad`, `src/glad.c` |
| GLFW | 3.x | `include/GLFW`, `lib/` |
| GLM | — | `include/glm` |
| Dear ImGui | v1.92.3 (docking) | `third_party/imgui` |
| ufbx | v0.23 (FBX 로더) | `third_party/ufbx` |

> 실행 시 작업 디렉터리는 **프로젝트 디렉터리**여야 한다 (`src/vs/*.vs` 같은 셰이더를 상대 경로로 읽는다).
> VS에서 F5로 실행하면 기본값이 그렇게 잡혀 있다.

## 조작

| 입력 | 동작 |
|---|---|
| 중간 버튼 드래그 | 궤도 회전 (orbit) |
| Shift + 중간 버튼 | 평행 이동 (pan) |
| 휠 | 줌 (거리에 비례) |
| Tab | 편집 모드 전환 (오브젝트 선택 상태에서) |
| 좌클릭 / 드래그 | 편집 모드에서 정점 선택 / 이동 |
| ESC | 종료 |

## FBX 가져오기

세 가지 방법이 있고 전부 같은 경로로 처리된다.

| 방법 | 비고 |
|---|---|
| 창에 파일을 **끌어다 놓기** | 여러 개를 한 번에 놓아도 된다 |
| **FBX 가져오기** 패널에 경로 입력 후 Enter | |
| `MiniBlender.exe 모델.fbx` | 탐색기에서 exe에 파일을 떨어뜨리는 경우 포함 |

불러오면 모델 전체가 화면에 들어오도록 카메라가 자동으로 맞춰지고,
시작 시 놓여 있던 예시용 큐브는 치워진다.

로더는 [ufbx](https://github.com/ufbx/ufbx)를 쓴다. FBX는 Autodesk 폐쇄 포맷이라 선택지가 셋뿐인데
—공식 FBX SDK(계정 등록 + 배포 제약), Assimp(별도 빌드 필요), ufbx(`.c`/`.h` 두 개)—
"클론 → VS로 열기 → F5"라는 이 프로젝트의 전제를 안 깨는 건 ufbx뿐이었다.

처리 과정에서 신경 쓴 것:

- **좌표계·단위 통일**: FBX는 만든 툴마다 축과 단위가 다르다(3ds Max는 Z-up, 유니티 에셋은 cm).
  `target_axes`와 `target_unit_meters`로 로드 시점에 변환해서, 우리 코드는 항상 "Y-up, 1유닛 = 1m"만 가정한다.
- **폴리곤 삼각형화**: FBX는 사각형 면을 흔히 쓰지만 OpenGL은 삼각형만 그린다.
- **노멀 생성**: 노멀이 없는 파일이 흔하다. 없으면 만들어 넣는다(안 그러면 조명이 새까맣게 나온다).
- **한글 경로**: `argv`는 콘솔 ANSI 코드페이지(CP949)로 들어오는데 ufbx는 UTF-8 경로를 기대한다.
  그래서 커맨드라인 인자는 `CommandLineToArgvW`로 원본 유니코드에서 다시 읽어 UTF-8로 변환한다.
  (드래그앤드롭은 GLFW가 UTF-8로 주므로 그대로 쓴다)

아직 재질과 텍스처는 읽지 않는다 — 전부 단색으로 그려진다.

## 내보내기 (OBJ / FBX)

**파일** 패널의 내보내기 칸에 경로를 넣고 저장하거나, CLI로 변환한다.
확장자로 포맷이 결정된다.

```bash
MiniBlender.exe 모델.fbx 결과.obj    # FBX -> OBJ
MiniBlender.exe 모델.fbx 결과.fbx    # FBX -> ASCII FBX
MiniBlender.exe 모델.obj 결과.fbx    # OBJ도 입력으로 받는다 (ufbx가 읽어준다)
```

두 익스포터의 결정적인 차이:

| | OBJ | FBX |
|---|---|---|
| 정점 좌표 | **월드 좌표로 구움** | **로컬 좌표 유지** |
| 위치/회전/크기 | 표현 불가 (좌표에 녹아듦) | `Model` 노드의 `Lcl Translation/Rotation/Scaling` |
| 단위·축 | 없음 | `UnitScaleFactor` / `UpAxis` |

즉 OBJ로 내보내면 트랜스폼이 사라지고, FBX로 내보내면 불러온 쪽에서 그대로 편집할 수 있다.

FBX는 **ASCII 7.4**로 쓴다. 바이너리는 노드마다 자식 블록의 끝 오프셋을 미리 적어야 해서
쓰는 도중엔 알 수 없는 값을 되돌아가 채워야 하고 배열 압축까지 얽힌다.
ASCII는 앞에서부터 순서대로 쓰면 끝이고 결과를 눈으로 검증할 수 있다. 대신 파일이 3~5배 크다.

작성하면서 걸린 함정 둘:

- **`PolygonVertexIndex`의 면 마지막 인덱스는 비트 반전(`~i`)해서 음수로 쓴다.** 그게 "면이 여기서 끝난다"는
  표시다. 빼먹으면 모든 삼각형이 하나의 거대한 폴리곤으로 이어져 형체가 뭉개진다.
- **`Connections` 섹션이 실질적인 씬 그래프다.** `Geometry`를 `Model`에, `Model`을 루트(0)에
  이어 붙이지 않으면 데이터는 파일에 있는데 화면엔 아무것도 안 나온다.

## 인스턴싱

같은 메시를 쓰는 오브젝트를 모아 **드로우콜 하나로** 그린다. 통계 패널의 체크박스로 껐다 켤 수 있다.

큐브 10,000개 기준 (RTX 3060 / Release / vsync off, 조건당 3회 최소값):

| | 드로우콜 | 삼각형 | CPU | GPU | 프레임 |
|---|---:|---:|---:|---:|---:|
| 끔 | 10,002 | 120,013 | 3.604 ms | 1.938 ms | 3.908 ms |
| **켬** | **2** | 120,013 | **1.182 ms** | **0.168 ms** | **1.514 ms** |

**삼각형 수가 두 줄이 똑같다는 게 요점이다.** GPU가 처리하는 양은 하나도 안 줄었고,
줄어든 건 "그려라"라고 말한 횟수뿐이다.

원리는 오브젝트마다 밀어넣던 값을 버퍼에 미리 쌓아두는 것이다. `model` 행렬과 색을 uniform이 아니라
정점 attribute로 넘기되, 바인딩 슬롯에 `glVertexArrayBindingDivisor(vao, 1, 1)`을 걸어
**정점마다가 아니라 인스턴스마다** 다음 칸으로 넘어가게 한다.

걸린 함정 셋:

- **`mat4`는 attribute 네 칸을 먹는다.** 한 칸의 최대가 `vec4`라 `model`이 location 2~5를 다 쓴다.
  그래서 색은 6번부터다. 3번에 색을 쓰면 행렬 둘째 열을 덮어써서 모델이 기괴하게 늘어난다.
- **켜둔 attribute는 버퍼가 없어도 읽으려 든다.** 그래서 `Upload` 시점에 미리 켜두지 않고,
  인스턴싱 경로가 그 메시를 처음 그릴 때 형식을 잡는다. 안 그러면 인스턴싱을 끈 경로가 정의되지 않은 동작이 된다.
- **프래그먼트 셰이더는 두 경로가 같은 파일을 쓴다.** 픽셀 칠하는 비용이 다르면
  나온 차이를 인스턴싱 덕이라고 말할 수 없다. 색만 uniform / attribute로 갈리고 나머지는 동일하다.

`Scene`과 `SceneObject`는 한 줄도 안 고쳤다. 오브젝트가 처음부터 메시를 포인터로 공유하고 있어서
같은 메시끼리 묶는 게 포인터 비교로 끝난다.

CPU 바운드가 아닌 씬에서는 **아무 효과가 없다.** 고해상도 구 64개(삼각형 103만)는
드로우콜이 66 → 3으로 줄어도 프레임이 0.297 → 0.292 ms로 오차 범위다.
그쪽은 GPU가 삼각형을 실제로 갈고 있고, 그 양은 인스턴싱으로 안 줄어들기 때문이다.
어느 쪽이 병목인지 보고 기법을 골라야 한다는 게 CPU/GPU를 나눠 표시하는 이유다.

자세한 측정과 과정은 [devlog #2](docs/devlog/02-인스턴싱.md)에.

## 정점 편집

오브젝트를 선택하고 **Tab**(또는 속성 패널의 "편집 모드")을 누르면 정점이 점으로 표시된다.
클릭해서 고르고, 그대로 드래그하면 화면 평면 위에서 움직인다. 속성 패널에서 좌표를 숫자로도 넣을 수 있다.

**핵심은 "용접(weld)"이다.** 불러온 메시는 삼각형마다 정점을 따로 갖는다 —
의자 모델을 재보니 정점 504개인데 실제 위치는 100개뿐이었다(한 자리에 평균 5개가 겹침).
면마다 노멀이 달라 공유가 안 되기 때문이다. 이 상태에서 정점 하나만 옮기면 그 삼각형의 모서리만
떨어져 나가 **메시가 찢어진다.** 그래서 편집 모드에 들어갈 때 같은 위치(0.1mm 격자)의 정점들을
한 덩어리로 묶고, 사용자가 옮기는 것은 그 덩어리다.

편집 후에는 움직인 정점이 속한 삼각형들의 **면 노멀을 다시 계산**한다.
평면 셰이딩 방식이라 원래 부드럽던 메시는 편집한 부분만 각져 보인다.

> VBO를 `glNamedBufferStorage(..., 0)`으로 만들면 내용 수정이 아예 거부된다.
> 편집을 위해 `GL_DYNAMIC_STORAGE_BIT`를 주고 `glNamedBufferSubData`로 갱신한다.

## 측정 모드

인자를 주고 실행하면 UI 없이 지정한 씬을 그린 뒤, 결과 한 줄을 찍고 자동 종료한다.

```
MiniBlender.exe <개수> [메시이름] [워밍업프레임] [샘플프레임] [--no-instancing]
```

```bash
> MiniBlender.exe 10000 Cube
[벤치] Cube x10000 (인스턴싱 켬, 워밍업 120프레임, 샘플 300프레임)
RESULT mesh=Cube n=10000 inst=on calls=2 tris=120013 cpu=1.182 gpu=0.168 frame=1.514 fps=660.4

> MiniBlender.exe 10000 Cube --no-instancing
[벤치] Cube x10000 (인스턴싱 끔, 워밍업 120프레임, 샘플 300프레임)
RESULT mesh=Cube n=10000 inst=off calls=10002 tris=120013 cpu=3.604 gpu=1.938 frame=3.908 fps=255.9
```

`--no-instancing`은 오브젝트 하나당 드로우콜 하나를 내는 원래 경로로 되돌린다.
기준선을 코드에서 지우지 않고 남겨둔 건, 기법을 넣을 때마다 같은 씬을 켜고/끄고 재서 비교하기 위해서다.

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
│   ├── Render/Benchmark     측정 모드 (인자 파싱 → 씬 구성 → 평균 출력)
│   ├── Render/Mesh          VAO/VBO/EBO + 프리미티브 생성기
│   ├── Render/OrbitCamera   DCC식 궤도 카메라
│   ├── Render/Renderer      씬 렌더링 (드로우콜 제출 / 인스턴싱 묶기)
│   ├── Scene/Scene          메시 라이브러리 + 오브젝트 목록
│   ├── Loader/FbxLoader     FBX/OBJ → 정점 데이터 (GL을 모른다)
│   ├── Loader/SceneImport   로더와 씬을 잇는 접착층 + 카메라 자동 맞춤
│   ├── Loader/ObjExporter   씬 → OBJ (정점을 월드 좌표로 구움)
│   ├── Loader/FbxExporter   씬 → ASCII FBX (트랜스폼을 Model 노드로 보존)
│   ├── Edit/EditMode        정점 편집 (용접 그룹, 피킹, 노멀 재계산)
│   └── UI/EditorUI          ImGui 패널 (통계 / 가져오기 / 아웃라이너 / 속성)
├── vs/, fs/                 셰이더
third_party/imgui/           Dear ImGui (vendored)
third_party/ufbx/            ufbx (vendored)
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
- **vsync와 CPU 값**: 평소 모드는 vsync가 켜져 있어서, 드라이버가 프레임 큐를 기다리는 시간이
  측정 구간 안에서 발생한다. 그래서 화면의 CPU 값이 프레임 주기(75Hz면 13.3ms)에 붙어 움직이지 않는다.
  순수 CPU 부하를 보려면 vsync를 끈 측정 모드를 쓸 것.

## 로드맵

- [x] **1단계 — 뼈대**: 메시/씬 구조, 궤도 카메라, 무한 그리드, 프레임 계측
- [~] **2단계 — 선택/조작**: 정점 편집(화면공간 피킹) 완료. ID 버퍼 피킹(FBO + `R32UI`), 스텐실 아웃라인, 트랜스폼 기즈모는 미구현
- [~] **3단계 — 드로우콜 최적화**: 인스턴싱 완료(같은 메시 N개 → 드로우콜 1개).
      행렬 캐싱, 프러스텀 컬링, `glMultiDrawElementsIndirect` + 버텍스 풀링,
      compute 기반 GPU 컬링은 미구현
- [ ] **4단계 — 셰이딩**: 디퍼드/포워드+, PBR, 그림자 맵, SSAO, HDR 톤매핑, MSAA

## 라이선스

- Dear ImGui — MIT (`third_party/imgui/LICENSE.txt`)
- ufbx — MIT 또는 Public Domain 택일 (`third_party/ufbx/LICENSE`)
