# MiniBlender
<img width="1284" height="752" alt="image" src="https://github.com/user-attachments/assets/fc5742e2-9605-4ff5-ad79-ed8ccf675c2c" />

C++/OpenGL 4.6으로 만드는 미니 3D 에디터.

## 빌드

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

---
# 기능
- FBX 입출력
- 정점 편집

## 최적화
- 인스턴싱


자세한 측정과 과정은 [devlog #2](docs/devlog/02-인스턴싱.md)에.


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

---
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
