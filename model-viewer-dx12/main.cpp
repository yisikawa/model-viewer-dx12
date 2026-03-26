#include "Application.h"

using namespace ModelViewer;


#ifdef _DEBUG
int main() {
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
#endif // DEBUG
	// WICやシェルダイアログのために COM を初期化（STAモード推奨）
	auto result = CoInitializeEx(0, COINIT_APARTMENTTHREADED);

	Application& app = Application::GetInstance();
	if (!app.Init()) {
		return -1;
	}
	app.Run();
	app.Terminate();
	return 0;
}