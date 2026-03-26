		_graphicsDevice->GetCommandQueue()->ExecuteCommandLists(1, cmdlists);

		// Wait for Draw
		_graphicsDevice->WaitDrawDone();

		_graphicsDevice->GetCommandAllocator()->Reset();
		_graphicsDevice->GetCommandList()->Reset(_graphicsDevice->GetCommandAllocator(), nullptr);

		// Flip / Present
		_graphicsDevice->GetSwapChain()->Present(2, 0);
	}
}

void Application::Terminate() {
	CleanupImGui();
}
